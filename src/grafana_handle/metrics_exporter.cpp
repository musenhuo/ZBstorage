#include "metrics_exporter.h"
#include "../../include/StorageResourceAPI.h"
#include "../../include/MetaServerMetrics.h"

#include <sstream>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <map>
#include <unordered_set>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

MetricsExporter::MetricsExporter(unsigned short port, int scrape_interval_seconds)
    : port_(port), scrape_interval_seconds_(scrape_interval_seconds), running_(false) {}

MetricsExporter::~MetricsExporter()
{
    stop();
}

void MetricsExporter::setMetricsProvider(const mds::metrics::IMetricsProvider *provider)
{
    metrics_provider_ = provider;
}

void MetricsExporter::start()
{
    if (running_.load())
        return;
    running_.store(true);
    // initial collect
    {
        std::lock_guard<std::mutex> lk(mu_);
        metrics_ = buildMetrics();
    }
    collector_thread_ = std::thread(&MetricsExporter::collectorLoop, this);
    server_thread_ = std::thread(&MetricsExporter::serverLoop, this);
}

void MetricsExporter::stop()
{
    if (!running_.load())
        return;
    running_.store(false);
    if (collector_thread_.joinable())
        collector_thread_.join();
    if (server_thread_.joinable())
        server_thread_.join();
}

std::string MetricsExporter::sanitizeLabel(const std::string &s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '\\')
            out += "\\\\";
        else if (c == '"')
            out += "\\\"";
        else if (c == '\n')
            out += "\\n";
        else
            out.push_back(c);
    }
    return out;
}

std::string MetricsExporter::buildMetrics()
{
    std::ostringstream ss;
    std::unordered_set<std::string> declared_metrics;

    using LabelList = std::vector<std::pair<std::string, std::string>>;
    auto emitGauge = [&](const std::string &name, const std::string &help, double value,
                         const LabelList &labels = {})
    {
        if (declared_metrics.insert(name).second)
        {
            ss << "# HELP " << name << ' ' << help << "\n";
            ss << "# TYPE " << name << " gauge\n";
        }
        ss << name;
        if (!labels.empty())
        {
            ss << '{';
            for (size_t i = 0; i < labels.size(); ++i)
            {
                if (i)
                    ss << ',';
                ss << labels[i].first << "=\"" << sanitizeLabel(labels[i].second) << "\"";
            }
            ss << '}';
        }
        ss << ' ' << value << "\n";
    };

    auto emitInfo = [&](const std::string &name, const std::string &help, const LabelList &labels)
    {
        emitGauge(name, help, 1.0, labels);
    };

    auto appendStorageMetrics = [&]()
    {
        try
        {
            OverallStorageInfo overall = GetOverallStorageInfo();
            std::vector<StorageNode> nodes = GetAllStorageNodes();
            OpticalLibrary optical = GetOpticalLibraryInfo();

            emitGauge("zb_total_storage_nodes", "Total number of storage nodes.",
                      static_cast<double>(overall.total_storage_nodes));
            emitGauge("zb_total_capacity_total", "Total capacity reported by storage layer.", overall.total_capacity);
            emitGauge("zb_total_used_total", "Total used capacity reported by storage layer.", overall.total_used);
            emitGauge("zb_optical_total_libraries", "Total optical library count.",
                      static_cast<double>(overall.total_optical_libraries));
            emitGauge("zb_optical_total_discs", "Total optical disc count.", static_cast<double>(overall.total_discs));
            emitGauge("zb_optical_total_capacity", "Aggregated optical capacity.", optical.total_capacity);

            for (const auto &n : nodes)
            {
                LabelList base{{"node_id", std::to_string(n.id)}, {"name", n.name}, {"status", n.status}};
                emitGauge("zb_storage_node_capacity", "Per-node capacity exposure.", n.capacity, base);
                emitGauge("zb_storage_node_used", "Per-node used capacity exposure.", n.used, base);
            }

            std::map<std::string, int> status_count;
            for (const auto &n : nodes)
                status_count[n.status]++;
            for (const auto &p : status_count)
            {
                emitGauge("zb_storage_node_status_count", "Count of storage nodes per status label.",
                          static_cast<double>(p.second), {{"status", p.first}});
            }
        }
        catch (const std::exception &e)
        {
            ss << "# Metrics collection failed: " << e.what() << "\n";
        }
        catch (...)
        {
            ss << "# Metrics collection threw unknown exception\n";
        }
    };

    auto appendMetaMetrics = [&]()
    {
        if (!metrics_provider_)
            return;
        try
        {
            const auto snapshot = metrics_provider_->collect_snapshot();

            const auto &ns = snapshot.namespace_scale;
            emitGauge("zb_mds_namespace_total_files", "Namespace file count reported by MDS.",
                      static_cast<double>(ns.total_files));
            emitGauge("zb_mds_namespace_total_directories", "Namespace directory count reported by MDS.",
                      static_cast<double>(ns.total_directories));
            emitGauge("zb_mds_namespace_max_depth", "Maximum namespace tree depth.",
                      static_cast<double>(ns.max_depth));
            for (size_t depth = 0; depth < ns.depth_histogram.size(); ++depth)
            {
                emitGauge("zb_mds_namespace_depth_histogram", "Directory count per depth level.",
                          static_cast<double>(ns.depth_histogram[depth]), {{"depth", std::to_string(depth)}});
            }
            static const std::vector<std::string> quantile_names{"p50", "p95", "p99"};
            for (size_t i = 0; i < ns.entries_per_dir_p99.size(); ++i)
            {
                std::string q = (i < quantile_names.size()) ? quantile_names[i] : ("p" + std::to_string(i));
                emitGauge("zb_mds_namespace_entries_per_dir", "Entries per directory percentiles.",
                          static_cast<double>(ns.entries_per_dir_p99[i]), {{"quantile", q}});
            }

            const auto &inode = snapshot.inode_pool;
            emitGauge("zb_mds_inode_total_slots", "Total inode slots provisioned.",
                      static_cast<double>(inode.total_slots));
            emitGauge("zb_mds_inode_allocated_slots", "Allocated inode slots.",
                      static_cast<double>(inode.allocated_slots));
            emitGauge("zb_mds_inode_allocation_rate_per_sec", "Inode allocation rate per second.",
                      inode.allocation_rate_per_sec);
            emitGauge("zb_mds_inode_recycle_rate_per_sec", "Inode recycle rate per second.",
                      inode.recycle_rate_per_sec);
            emitGauge("zb_mds_inode_fragmentation_ratio", "Inode bitmap fragmentation ratio.",
                      inode.fragmentation_ratio);
            emitGauge("zb_mds_inode_allocation_failures", "Total inode allocation failures.",
                      static_cast<double>(inode.allocation_failures));
            for (const auto &p : inode.failure_reason_breakdown)
            {
                emitGauge("zb_mds_inode_allocation_failures_total", "Inode allocation failures per reason.",
                          static_cast<double>(p.second), {{"reason", p.first}});
            }

            const auto emitTimeline = [&](const std::string &op_name, const mds::metrics::OperationTimeline &timeline)
            {
                emitGauge("zb_mds_operation_qps", "Operation QPS per verb.", timeline.qps, {{"op", op_name}});
                emitGauge("zb_mds_operation_success_rate", "Operation success rate per verb.",
                          timeline.success_rate, {{"op", op_name}});
                emitGauge("zb_mds_operation_queue_length", "Operation queue depth per verb.",
                          static_cast<double>(timeline.queue_length), {{"op", op_name}});
                for (const auto &p : timeline.failure_reasons)
                {
                    emitGauge("zb_mds_operation_failures_total", "Operation failures per reason.",
                              static_cast<double>(p.second), {{"op", op_name}, {"reason", p.first}});
                }
                for (const auto &p : timeline.latency_percentiles)
                {
                    emitGauge("zb_mds_operation_latency_seconds",
                              "Observed latency percentiles for each operation.", p.second,
                              {{"op", op_name}, {"quantile", p.first}});
                }
            };

            emitTimeline("mkdir", snapshot.operations.mkdir);
            emitTimeline("create", snapshot.operations.create);
            emitTimeline("remove", snapshot.operations.remove);
            emitTimeline("rmdir", snapshot.operations.rmdir);
            emitTimeline("lookup", snapshot.operations.lookup);
            emitTimeline("ls", snapshot.operations.ls);

            const auto &cache = snapshot.cache;
            emitGauge("zb_mds_cache_hit_ratio", "Cache hit ratio for inode/index data.", cache.hit_ratio);
            emitGauge("zb_mds_cache_current_entries", "Current cache entry count.",
                      static_cast<double>(cache.current_entries));
            emitGauge("zb_mds_cache_max_entries", "Configured cache capacity.",
                      static_cast<double>(cache.max_entries));
            emitGauge("zb_mds_cache_rebuild_duration_ms", "Duration of the last cache rebuild (ms).",
                      static_cast<double>(cache.rebuild_duration.count()));
            if (cache.last_rebuild_time)
            {
                double ts = std::chrono::duration_cast<std::chrono::seconds>(
                                cache.last_rebuild_time->time_since_epoch())
                                .count();
                emitGauge("zb_mds_cache_last_rebuild_time_seconds", "Unix timestamp for last cache rebuild.", ts);
            }

            const auto &pers = snapshot.persistence;
            emitGauge("zb_mds_persistence_inode_file_size_bytes", "Size of inode persistence file.",
                      static_cast<double>(pers.inode_file_size_bytes));
            emitGauge("zb_mds_persistence_bitmap_file_size_bytes", "Size of bitmap persistence file.",
                      static_cast<double>(pers.bitmap_file_size_bytes));
            emitGauge("zb_mds_persistence_expansion_count", "Persistence expansion count.",
                      static_cast<double>(pers.expansion_count));
            emitGauge("zb_mds_persistence_last_expansion_cost_ms", "Duration of last expansion (ms).",
                      static_cast<double>(pers.last_expansion_cost.count()));
            emitGauge("zb_mds_persistence_bitmap_flush_period_seconds",
                      "Bitmap flush period in seconds.", static_cast<double>(pers.bitmap_flush_period.count()));
            if (pers.last_bitmap_flush_time)
            {
                double ts = std::chrono::duration_cast<std::chrono::seconds>(
                                pers.last_bitmap_flush_time->time_since_epoch())
                                .count();
                emitGauge("zb_mds_persistence_last_bitmap_flush_time_seconds",
                          "Unix timestamp of last bitmap flush.", ts);
            }
            emitGauge("zb_mds_persistence_recent_failure_count", "Number of recent persistence failures held in memory.",
                      static_cast<double>(pers.persistence_failures.size()));
            const size_t failure_limit = 20;
            for (size_t i = 0; i < pers.persistence_failures.size() && i < failure_limit; ++i)
            {
                emitInfo("zb_mds_persistence_failure_info", "Recent persistence failure descriptions.",
                         {{"index", std::to_string(i)}, {"message", pers.persistence_failures[i]}});
            }

            const auto &time_metrics = snapshot.time_attributes;
            auto emitTimeHistogram = [&](const std::map<std::string, uint64_t> &hist, const std::string &metric_name,
                                         const std::string &help)
            {
                for (const auto &p : hist)
                {
                    emitGauge(metric_name, help, static_cast<double>(p.second), {{"bucket", p.first}});
                }
            };
            emitTimeHistogram(time_metrics.atime_histogram, "zb_mds_time_atime_bucket",
                              "Access time histogram bucket counts.");
            emitTimeHistogram(time_metrics.mtime_histogram, "zb_mds_time_mtime_bucket",
                              "Modification time histogram bucket counts.");
            emitTimeHistogram(time_metrics.ctime_histogram, "zb_mds_time_ctime_bucket",
                              "Change time histogram bucket counts.");
            emitGauge("zb_mds_time_cold_inode_candidates_total", "Cold inode candidate list size.",
                      static_cast<double>(time_metrics.cold_inode_candidates.size()));
            const size_t cold_limit = 20;
            for (size_t i = 0; i < time_metrics.cold_inode_candidates.size() && i < cold_limit; ++i)
            {
                emitInfo("zb_mds_time_cold_inode_candidate", "Sample of cold inode candidates (limited).",
                         {{"index", std::to_string(i)},
                          {"inode", std::to_string(time_metrics.cold_inode_candidates[i])}});
            }

            const auto &bg = snapshot.background_tasks;
            emitGauge("zb_mds_background_scan_period_seconds", "Cold data scan period in seconds.",
                      static_cast<double>(bg.scan_period.count()));
            emitGauge("zb_mds_background_candidate_count", "Cold scan candidate count.",
                      static_cast<double>(bg.candidate_count));
            emitGauge("zb_mds_background_scan_duration_ms", "Duration of last cold scan (ms).",
                      static_cast<double>(bg.scan_duration.count()));
            emitGauge("zb_mds_background_data_plane_progress", "Data plane progress ratio during scan.",
                      bg.data_plane_progress);
            if (!bg.trigger_reason.empty())
            {
                emitInfo("zb_mds_background_trigger_reason", "Reason for last background scan trigger.",
                         {{"reason", bg.trigger_reason}});
            }

            const auto &quotas = snapshot.quotas;
            for (const auto &vol : quotas.volumes)
            {
                LabelList labels{{"volume_id", vol.volume_id}};
                emitGauge("zb_mds_quota_volume_logical_bytes", "Logical bytes per volume.",
                          static_cast<double>(vol.logical_bytes), labels);
                emitGauge("zb_mds_quota_volume_physical_bytes", "Physical bytes per volume.",
                          static_cast<double>(vol.physical_bytes), labels);
                emitGauge("zb_mds_quota_volume_block_segments", "Block segments per volume.",
                          static_cast<double>(vol.block_segments), labels);
            }
            for (const auto &dir : quotas.directories)
            {
                LabelList labels{{"path", dir.path}};
                emitGauge("zb_mds_quota_directory_logical_bytes", "Logical bytes per directory.",
                          static_cast<double>(dir.logical_bytes), labels);
                emitGauge("zb_mds_quota_directory_entry_count", "Entry count per directory.",
                          static_cast<double>(dir.entry_count), labels);
            }
            emitGauge("zb_mds_quota_orphan_inode_count", "Total orphan inode count tracked by MDS.",
                      static_cast<double>(quotas.orphan_inodes.size()));
            const size_t orphan_limit = 20;
            for (size_t i = 0; i < quotas.orphan_inodes.size() && i < orphan_limit; ++i)
            {
                emitInfo("zb_mds_quota_orphan_inode", "Sample of orphan inode IDs (limited).",
                         {{"index", std::to_string(i)}, {"inode", std::to_string(quotas.orphan_inodes[i])}});
            }

            const auto &topology = snapshot.topology;
            for (const auto &node : topology.nodes)
            {
                LabelList labels{{"node_id", node.node_id}, {"role", node.role}};
                emitGauge("zb_mds_topology_node_healthy", "Node heartbeat health (1=healthy).",
                          node.healthy ? 1.0 : 0.0, labels);
                double hb = std::chrono::duration_cast<std::chrono::seconds>(
                                node.last_heartbeat.time_since_epoch())
                                .count();
                emitGauge("zb_mds_topology_last_heartbeat_seconds", "Last heartbeat timestamp per node.", hb, labels);
            }

            const auto &audit = snapshot.audit;
            emitGauge("zb_mds_audit_alert_count", "Alert count in recent window.",
                      static_cast<double>(audit.alert_count));
            emitGauge("zb_mds_audit_restart_count", "Restart count in recent window.",
                      static_cast<double>(audit.restart_count));
            const size_t audit_limit = 20;
            for (size_t i = 0; i < audit.recent_alerts.size() && i < audit_limit; ++i)
            {
                emitInfo("zb_mds_audit_recent_alert", "Recent audit alert annotations.",
                         {{"index", std::to_string(i)}, {"message", audit.recent_alerts[i]}});
            }
            for (size_t i = 0; i < audit.recent_config_changes.size() && i < audit_limit; ++i)
            {
                emitInfo("zb_mds_audit_config_change", "Recent config change annotations.",
                         {{"index", std::to_string(i)}, {"message", audit.recent_config_changes[i]}});
            }
        }
        catch (const std::exception &e)
        {
            ss << "# MetaServer metrics collection failed: " << e.what() << "\n";
        }
        catch (...)
        {
            ss << "# MetaServer metrics collection threw unknown exception\n";
        }
    };

    appendStorageMetrics();
    appendMetaMetrics();

    return ss.str();
}

void MetricsExporter::collectorLoop()
{
    while (running_.load())
    {
        std::string next = buildMetrics();
        {
            std::lock_guard<std::mutex> lk(mu_);
            metrics_.swap(next);
        }
        for (int i = 0; i < scrape_interval_seconds_ && running_.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void MetricsExporter::serverLoop()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "metrics exporter: socket failed\n";
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "metrics exporter: bind failed\n";
        close(server_fd);
        return;
    }

    if (listen(server_fd, 8) < 0)
    {
        std::cerr << "metrics exporter: listen failed\n";
        close(server_fd);
        return;
    }

    while (running_.load())
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            if (!running_.load())
                break;
            continue;
        }

        // Read request (very small simple HTTP handling)
        char buf[4096];
        ssize_t r = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (r <= 0)
        {
            close(client_fd);
            continue;
        }
        buf[r] = '\0';
        std::string req(buf);

        // Only respond to GET /metrics
        bool isMetrics = false;
        if (req.rfind("GET ", 0) == 0)
        {
            size_t pos = req.find(' ', 4);
            if (pos != std::string::npos)
            {
                std::string path = req.substr(4, pos - 4);
                if (path == "/metrics")
                    isMetrics = true;
            }
        }

        std::string body;
        if (isMetrics)
        {
            std::lock_guard<std::mutex> lk(mu_);
            body = metrics_;
        }
        else
        {
            body = "not found\n";
        }

        std::ostringstream resp;
        if (isMetrics)
        {
            resp << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
        }
        else
        {
            resp << "HTTP/1.1 404 Not Found\r\n"
                 << "Content-Type: text/plain; charset=utf-8\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
        }

        std::string respstr = resp.str();
        send(client_fd, respstr.c_str(), respstr.size(), 0);
        close(client_fd);
    }

    close(server_fd);
}
