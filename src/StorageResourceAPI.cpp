#include "StorageResourceAPI.h"
#include <vector>
#include <string>

// Mock data for demonstration purposes
std::vector<StorageNode> storageNodes = {
    {1, "Node1", "Active", 1000.0, 500.0},
    {2, "Node2", "Inactive", 2000.0, 1500.0}
};

OpticalLibrary opticalLibrary = {100, 5000.0, 3000.0};

std::vector<StorageNode> GetAllStorageNodes() {
    return storageNodes;
}

OpticalLibrary GetOpticalLibraryInfo() {
    return opticalLibrary;
}

std::string GetStorageResourceStatus(int resource_id) {
    for (const auto& node : storageNodes) {
        if (node.id == resource_id) {
            return node.status;
        }
    }
    return "Resource not found";
}

std::string GetStorageStatistics() {
    double totalCapacity = 0.0;
    double totalUsed = 0.0;
    for (const auto& node : storageNodes) {
        totalCapacity += node.capacity;
        totalUsed += node.used;
    }
    return "Total Capacity: " + std::to_string(totalCapacity) + ", Total Used: " + std::to_string(totalUsed);
}

OverallStorageInfo GetOverallStorageInfo() {
    OverallStorageInfo info;
    info.total_storage_nodes = storageNodes.size();
    info.total_capacity = 0.0;
    info.total_used = 0.0;
    info.total_optical_libraries = 1; // Mock data
    info.total_discs = opticalLibrary.disc_count;

    for (const auto& node : storageNodes) {
        info.total_capacity += node.capacity;
        info.total_used += node.used;
    }

    return info;
}