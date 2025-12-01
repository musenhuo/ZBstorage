#ifndef GRAFANA_METRICS_EXPORTER_H
#define GRAFANA_METRICS_EXPORTER_H

#include <string>
#include <thread>
#include <mutex>
#include <atomic>

namespace mds::metrics
{
    class IMetricsProvider;
}

class MetricsExporter
{
public:
    explicit MetricsExporter(unsigned short port = 9100, int scrape_interval_seconds = 5);
    ~MetricsExporter();

    void start();
    void stop();

    void setMetricsProvider(const mds::metrics::IMetricsProvider *provider);

private:
    unsigned short port_;
    int scrape_interval_seconds_;

    const mds::metrics::IMetricsProvider *metrics_provider_ = nullptr;

    std::string metrics_; // latest metrics in Prometheus text format
    std::mutex mu_;

    std::thread collector_thread_;
    std::thread server_thread_;
    std::atomic<bool> running_;

    void collectorLoop();
    void serverLoop();
    std::string buildMetrics();
    std::string sanitizeLabel(const std::string &s);
};

#endif // GRAFANA_METRICS_EXPORTER_H
