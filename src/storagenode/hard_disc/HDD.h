#pragma once
#include "StorageTypes.h"
#include "Device.h"
#include <nlohmann/json.hpp>

class HardDiskDrive : public Device {
public:
    double write_throughput_MBps; // 写吞吐率（MB/s）
    double read_throughput_MBps;  // 读吞吐率（MB/s）
    uint64_t remaining_space;     // 剩余空间（字节）

    // 构造函数，初始化容量和吞吐率
    HardDiskDrive(const std::string& id,
                  uint64_t cap = HDD_DEFAULT_CAPACITY,
                  double write_tp = HDD_DEFAULT_WRITE_MBPS,
                  double read_tp = HDD_DEFAULT_READ_MBPS);

    // 写操作，返回预计写入时间（秒）
    double write(uint64_t offset, uint64_t length) override;

    // 读操作，返回预计读取时间（秒）
    double read(uint64_t offset, uint64_t length) const override;

    std::string type() const override { return "HardDiskDrive"; }
    nlohmann::json to_json() const override;
};
