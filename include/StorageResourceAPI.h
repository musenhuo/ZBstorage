#ifndef STORAGE_RESOURCE_API_H
#define STORAGE_RESOURCE_API_H

#include <string>
#include <vector>

// Struct to represent a storage node
struct StorageNode {
    int id;
    std::string name;
    std::string status;
    double capacity;
    double used;
};

// Struct to represent optical library information
struct OpticalLibrary {
    int disc_count;
    double total_capacity;
    double used_capacity;
};

// Function to get all storage nodes information
std::vector<StorageNode> GetAllStorageNodes();

// Function to get optical library information
OpticalLibrary GetOpticalLibraryInfo();

// Function to get the status of a specific storage resource
std::string GetStorageResourceStatus(int resource_id);

// Function to get storage statistics
std::string GetStorageStatistics();

// Function to get overall storage information
struct OverallStorageInfo {
    int total_storage_nodes;
    double total_capacity;
    double total_used;
    int total_optical_libraries; // Total number of optical libraries
    int total_discs; // Total number of discs
};

OverallStorageInfo GetOverallStorageInfo();

#endif // STORAGE_RESOURCE_API_H