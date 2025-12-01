#include "SSD.h"
#include <nlohmann/json.hpp>
#include <iostream>

SolidStateDrive::SolidStateDrive(const std::string& id,
                                 uint64_t cap,
                                 double write_tp,
                                 double read_tp)
    : Device(id, cap),
      write_throughput_MBps(write_tp),
      read_throughput_MBps(read_tp) {
    remaining_space = cap; // 初始化剩余空间为总容量
}

double SolidStateDrive::write(uint64_t offset, uint64_t length) {
    (void)offset; // 当前模型未用到 offset，避免未使用告警
    if (length > remaining_space) {
        std::cerr << "SSD空间不足，仅写入剩余空间: " << remaining_space << " 字节\n";
        length = remaining_space;
    }
    remaining_space -= length;
    // 返回写操作预计耗时（秒）
    return static_cast<double>(length) / (write_throughput_MBps * 1024.0 * 1024.0);
}

double SolidStateDrive::read(uint64_t offset, uint64_t length) const {
    (void)offset;
    // 返回读操作预计耗时（秒）
    return static_cast<double>(length) / (read_throughput_MBps * 1024.0 * 1024.0);
}

nlohmann::json SolidStateDrive::to_json() const {
    nlohmann::json j = Device::to_json();
    j["write_throughput_MBps"] = write_throughput_MBps;
    j["read_throughput_MBps"]  = read_throughput_MBps;
    return j;
}
