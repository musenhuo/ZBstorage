#include "OpticalDisc.h"
#include <nlohmann/json.hpp>
#include <cstring>   // strncpy
#include <string>

OpticalDisc::OpticalDisc(const std::string& id,
                         const std::string& library_id,
                         uint64_t cap,
                         double write_tp,
                         double read_tp)
    : capacity(cap),
      status(DiscStatus::Blank),
      write_throughput_MBps(write_tp),
      read_throughput_MBps(read_tp) 
{
    std::strncpy(device_id, id.c_str(), sizeof(device_id));
    device_id[sizeof(device_id) - 1] = '\0'; // 保证结尾有 '\0'

    std::strncpy(this->library_id, library_id.c_str(), sizeof(this->library_id));
    this->library_id[sizeof(this->library_id) - 1] = '\0'; // 保证结尾有 '\0'
}

double OpticalDisc::burnImage(uint64_t img_size) {
    if (status != DiscStatus::Blank) return -1.0;
    if (img_size > capacity) return -1.0;
    status = DiscStatus::Finalized;
    // MB/s -> 字节/秒：MB * 1024 * 1024
    return static_cast<double>(img_size) / (write_throughput_MBps * 1024.0 * 1024.0);
}

double OpticalDisc::read(uint64_t offset, uint64_t length) const {
    (void)offset; // 当前模型未使用偏移，保留参数避免未使用告警
    if (status != DiscStatus::Finalized) return -1.0;
    return static_cast<double>(length) / (read_throughput_MBps * 1024.0 * 1024.0);
}

DiscStatus OpticalDisc::getStatus() const {
    return status;
}

nlohmann::json OpticalDisc::to_json() const {
    nlohmann::json j;
    j["device_id"] = device_id;
    j["library_id"] = library_id;
    j["capacity"] = capacity;
    j["status"] = static_cast<int>(status);
    j["write_throughput_MBps"] = write_throughput_MBps;
    j["read_throughput_MBps"]  = read_throughput_MBps;
    return j;
}
