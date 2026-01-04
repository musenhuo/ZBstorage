#include <brpc/server.h>
#include <gflags/gflags.h>

#include <chrono>
#include <iostream>
#include <memory>

#include "ClusterManagerServiceImpl.h"
#include "StorageNodeManager.h"

DEFINE_int32(srm_port, 9100, "Port for SRM cluster manager service");
DEFINE_int32(heartbeat_timeout_sec, 30, "Heartbeat timeout in seconds");
DEFINE_int32(health_check_interval_sec, 10, "Health monitor interval in seconds");

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    auto manager = std::make_shared<StorageNodeManager>(
        std::chrono::seconds(FLAGS_heartbeat_timeout_sec),
        std::chrono::seconds(FLAGS_health_check_interval_sec));
    manager->Start();

    ClusterManagerServiceImpl service(manager);

    brpc::Server server;
    if (server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        std::cerr << "Failed to add ClusterManagerService" << std::endl;
        return -1;
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;

    if (server.Start(FLAGS_srm_port, &options) != 0) {
        std::cerr << "Failed to start SRM server on port " << FLAGS_srm_port << std::endl;
        return -1;
    }

    std::cout << "SRM ClusterManagerService started on port " << FLAGS_srm_port
              << " heartbeat_timeout_sec=" << FLAGS_heartbeat_timeout_sec
              << " health_check_interval_sec=" << FLAGS_health_check_interval_sec
              << std::endl;
    server.RunUntilAskedToQuit();
    manager->Stop();
    return 0;
}
