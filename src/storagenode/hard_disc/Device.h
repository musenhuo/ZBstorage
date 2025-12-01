#pragma once
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

class Device {
public:
    std::string device_id;   // 设备唯一标识
    uint64_t capacity;       // 总容量（字节）

    Device(std::string id, uint64_t cap)
        : device_id(std::move(id)), capacity(cap) {}

    virtual ~Device() = default;

    // 纯虚函数：读操作，返回预计耗时（秒）
    virtual double read(uint64_t offset, uint64_t length) const = 0;

    // 纯虚函数：写操作，返回预计耗时（秒）
    virtual double write(uint64_t offset, uint64_t length) = 0;

    // 获取设备类型
    virtual std::string type() const = 0;

    // 设备信息转json
    virtual nlohmann::json to_json() const {
        nlohmann::json j;
        j["device_id"] = device_id;
        j["capacity"] = capacity;
        j["type"] = type();
        return j;
    }
};
