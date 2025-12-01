#include "LocalStorageGateway.h"
#include <iostream>

extern StorageResource* g_storage_resource;

double LocalStorageGateway::processIO(const IORequest& req) {
    if (!g_storage_resource) {
        std::cerr << "[LocalStorageGateway] g_storage_resource 未初始化" << std::endl;
        return -1.0;
    }
    return g_storage_resource->processIO(req);
}

void LocalStorageGateway::processIOBatch(const std::vector<IORequest>& reqs) {
    if (!g_storage_resource) {
        std::cerr << "[LocalStorageGateway] g_storage_resource 未初始化" << std::endl;
        return;
    }
    for (const auto& req : reqs) {
        g_storage_resource->processIO(req);
    }
}