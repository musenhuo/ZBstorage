#pragma once
#include "StorageTypes.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

class OpticalDisc {
public:
    char device_id[32];   // 设备唯一标识
    char library_id[16];  // 光盘库唯一标识
    uint64_t capacity;       // 总容量（字节）
    DiscStatus status;        // 当前状态
    double write_throughput_MBps; // 写吞吐率（MB/s）
    double read_throughput_MBps;  // 读吞吐率（MB/s）

    // 构造函数，带默认值
    OpticalDisc(const std::string& id,
                const std::string& library_id = "lab_0",
                uint64_t cap = OPTICAL_DISC_CAPACITY, // 默认100GB
                double write_tp = OPTICAL_DISC_WRITE_MBPS, // 默认写36MB/s
                double read_tp = OPTICAL_DISC_READ_MBPS);  // 默认读36MB/s

    double burnImage(uint64_t img_size);
    double read(uint64_t offset, uint64_t length) const;
    DiscStatus getStatus() const;
    std::string type() const  { return "OpticalDisc"; }
    nlohmann::json to_json() const; 
};

struct OpticalDiscPtrLess {
    bool operator()(const std::shared_ptr<OpticalDisc>& a, const std::shared_ptr<OpticalDisc>& b) const {
        return std::string(a->device_id) < std::string(b->device_id);
    }
};
