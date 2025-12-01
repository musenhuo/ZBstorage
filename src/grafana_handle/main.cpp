#include "metrics_exporter.h"
#include "../../include/MetaServerMetrics.h"

#include <iostream>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <thread>

#if defined(__GNUC__)
__attribute__((weak)) const mds::metrics::IMetricsProvider *GetMetaServerMetricsProvider()
{
    return nullptr;
}
#else
const mds::metrics::IMetricsProvider *GetMetaServerMetricsProvider()
{
    return nullptr;
}
#endif

static MetricsExporter *g_exporter = nullptr;

void handle_sigint(int)
{
    if (g_exporter)
        g_exporter->stop();
    std::exit(0);
}

int main(int argc, char **argv)
{
    unsigned short port = 9100;
    int interval = 5;
    if (const char *env = std::getenv("METRICS_PORT"))
        port = static_cast<unsigned short>(std::atoi(env));
    if (const char *env2 = std::getenv("METRICS_INTERVAL"))
        interval = std::atoi(env2);

    MetricsExporter exporter(port, interval);
    g_exporter = &exporter;

    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    std::cout << "Starting metrics exporter on port " << port << ", interval=" << interval << "s\n";
    if (const auto *provider = GetMetaServerMetricsProvider())
    {
        exporter.setMetricsProvider(provider);
        std::cout << "MetaServer metrics provider attached" << std::endl;
    }
    else
    {
        std::cout << "MetaServer metrics provider not linked; exporter will expose storage metrics only" << std::endl;
    }
    exporter.start();

    // main thread just sleeps while exporter runs
    while (true)
        std::this_thread::sleep_for(std::chrono::hours(24));

    return 0;
}
