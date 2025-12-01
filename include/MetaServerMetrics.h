#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief 统一定义 MDS 暴露给监控/可视化层（Grafana 等）的度量数据接口。
 *        所有字段仅描述数据模型，不包含具体实现。
 */
namespace mds::metrics
{

    /**
     * @brief 命名空间规模（文件系统树状结构）指标。
     */
    struct NamespaceScaleMetrics
    {
        uint64_t total_files = 0;                  ///< 当前总文件数量。
        uint64_t total_directories = 0;            ///< 当前总目录数量。
        uint32_t max_depth = 0;                    ///< 当前目录层级最大深度。
        std::vector<uint64_t> depth_histogram;     ///< depth_histogram[d] = 深度为 d 的目录节点数量。
        std::vector<uint64_t> entries_per_dir_p99; ///< 每层目录子项数量的分位统计（自定义分位线数组 50/95/99）。
    };

    /**
     * @brief inode 管理相关指标。
     */
    struct InodePoolMetrics
    {
        uint64_t total_slots = 0;                                 ///< inode 存储的总槽位数。
        uint64_t allocated_slots = 0;                             ///< 已分配 inode 槽位数。
        double allocation_rate_per_sec = 0.0;                     ///< 最近时间窗口内分配速率（个/秒）。
        double recycle_rate_per_sec = 0.0;                        ///< 最近时间窗口内释放速率（个/秒）。
        double fragmentation_ratio = 0.0;                         ///< 空闲位图碎片率（0~1）。
        uint64_t allocation_failures = 0;                         ///< 分配失败总次数。
        std::map<std::string, uint64_t> failure_reason_breakdown; ///< 分配失败原因分布。
    };

    /**
     * @brief 单类操作的吞吐、成功率与延迟指标。
     */
    struct OperationTimeline
    {
        double qps = 0.0;                                  ///< 每秒请求数。
        double success_rate = 1.0;                         ///< 成功率（0~1）。
        std::map<std::string, uint64_t> failure_reasons;   ///< 失败原因分布。
        uint32_t queue_length = 0;                         ///< 后台排队长度。
        std::map<std::string, double> latency_percentiles; ///< 延迟分位数（例如 {"p50":1.2,"p95":3.4}）。
    };

    /**
     * @brief 文件系统操作吞吐的整体视图。
     */
    struct OperationMetrics
    {
        OperationTimeline mkdir;
        OperationTimeline create;
        OperationTimeline remove;
        OperationTimeline rmdir;
        OperationTimeline lookup;
        OperationTimeline ls;
    };

    /**
     * @brief inode_table_ 相关缓存指标。
     */
    struct CacheAndIndexMetrics
    {
        double hit_ratio = 1.0;                                                 ///< 命中率（0~1）。
        size_t current_entries = 0;                                             ///< 当前表项数量。
        size_t max_entries = 0;                                                 ///< 缓存容量上限（若有）。
        std::chrono::milliseconds rebuild_duration{};                           ///< 最近一次重建耗时。
        std::optional<std::chrono::system_clock::time_point> last_rebuild_time; ///< 最近重建时间。
    };

    /**
     * @brief 元数据持久化文件与位图管理指标。
     */
    struct PersistenceMetrics
    {
        uint64_t inode_file_size_bytes = 0;                                          ///< inode 存储文件大小。
        uint64_t bitmap_file_size_bytes = 0;                                         ///< 位图文件大小。
        uint64_t expansion_count = 0;                                                ///< 扩容次数。
        std::chrono::milliseconds last_expansion_cost{};                             ///< 最近一次扩容耗时。
        std::chrono::seconds bitmap_flush_period{};                                  ///< 位图持久化周期。
        std::optional<std::chrono::system_clock::time_point> last_bitmap_flush_time; ///< 最近一次写入时间。
        std::vector<std::string> persistence_failures;                               ///< 最近失败/重试记录（字符串带原因描述）。
    };

    /**
     * @brief 文件时间属性的分布情况与冷数据候选。
     */
    struct TimeAttributeMetrics
    {
        std::map<std::string, uint64_t> atime_histogram; ///< 访问时间分段统计。
        std::map<std::string, uint64_t> mtime_histogram; ///< 修改时间分段统计。
        std::map<std::string, uint64_t> ctime_histogram; ///< 状态变更时间分段统计。
        std::vector<uint64_t> cold_inode_candidates;     ///< 冷数据候选 inode 列表。
    };

    /**
     * @brief 后台任务执行情况。
     */
    struct BackgroundTaskMetrics
    {
        std::chrono::seconds scan_period{};        ///< 冷数据扫描周期。
        uint64_t candidate_count = 0;              ///< 最近一次扫描候选数量。
        std::chrono::milliseconds scan_duration{}; ///< 最近一次扫描耗时。
        std::string trigger_reason;                ///< 最近触发条件描述。
        double data_plane_progress = 0.0;          ///< 与数据面交互进度（0~1）。
    };

    /**
     * @brief 配额与资源使用指标。
     */
    struct QuotaAndResourceMetrics
    {
        struct VolumeUsage
        {
            std::string volume_id;
            uint64_t logical_bytes = 0;  ///< 卷上逻辑占用字节数。
            uint64_t physical_bytes = 0; ///< 卷上物理占用字节数。
            uint64_t block_segments = 0; ///< 已分配块段数量。
        };

        struct DirectoryUsage
        {
            std::string path;
            uint64_t logical_bytes = 0;
            uint64_t entry_count = 0;
        };

        std::vector<VolumeUsage> volumes;
        std::vector<DirectoryUsage> directories;
        std::vector<uint64_t> orphan_inodes; ///< 未回收 inode 清单。
    };

    /**
     * @brief 集群节点与心跳信息。
     */
    struct ClusterTopologyMetrics
    {
        struct NodeInfo
        {
            std::string node_id;
            std::string role;                                     ///< 节点角色（MDS/Client/Storage 等）。
            bool healthy = true;                                  ///< 心跳是否正常。
            std::chrono::system_clock::time_point last_heartbeat; ///< 最近心跳时间戳。
        };

        std::vector<NodeInfo> nodes;
    };

    /**
     * @brief 日志与审计相关指标。
     */
    struct AuditLogMetrics
    {
        uint64_t alert_count = 0;                       ///< 异常告警数量（可按时间窗口统计）。
        uint64_t restart_count = 0;                     ///< 最近周期内重启/崩溃次数。
        std::vector<std::string> recent_alerts;         ///< 最近告警摘要。
        std::vector<std::string> recent_config_changes; ///< 配置变更历史摘要。
    };

    /**
     * @brief 汇总所有监控维度的顶级对象。
     */
    struct ServerMetricsSnapshot
    {
        NamespaceScaleMetrics namespace_scale;
        InodePoolMetrics inode_pool;
        OperationMetrics operations;
        CacheAndIndexMetrics cache;
        PersistenceMetrics persistence;
        TimeAttributeMetrics time_attributes;
        BackgroundTaskMetrics background_tasks;
        QuotaAndResourceMetrics quotas;
        ClusterTopologyMetrics topology;
        AuditLogMetrics audit;
    };

    /**
     * @brief MDS 需要实现的指标抓取接口，供外部导出组件（REST/Prometheus exporter）调用。
     */
    class IMetricsProvider
    {
    public:
        virtual ~IMetricsProvider() = default;

        /**
         * @brief 拉取一个当前快照。建议调用方定期采样并下发至 Grafana。
         */
        virtual ServerMetricsSnapshot collect_snapshot() const = 0;
    };

} // namespace mds::metrics
