#pragma once
#include "OpticalDisc.h"
#include <map>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

class OpticalDiscLibrary {
public:
    std::string library_id;                          // 光盘库唯一标识
    std::map<int, std::string> non_default_discs;    // 非默认光盘槽位及其光盘id（key: 槽位id, value: 光盘id）
    std::vector<int> miss_slots;                     // 不存在光盘的槽号容器
    uint16_t disc_num;
    uint32_t drive_count;                            // 光驱数量
    double load_unload_time;                         // 装载/卸载光盘时间（秒）

    OpticalDiscLibrary(std::string id = "0",
                       uint16_t disc_num = OPTICAL_LIBRARY_DISC_NUM,
                       uint32_t drive_num = OPTICAL_LIBRARY_DRIVE_COUNT,
                       double load_time = OPTICAL_LIBRARY_LOAD_TIME);

    // 添加一张新光盘
    void addDisc(const std::string& disc_id);

    // 按ID查找光盘（若存在则返回槽号）
    int hasDisc(const std::string& disc_id) const;

    // 移除光盘，返回是否成功
    bool removeDisc(const std::string& disc_id);

    // 模拟刻录，返回总耗时（含装载时间和刻录时间）
    double burnToDisc(const std::string& disc_id, uint64_t img_size);

    // 模拟读取，返回总耗时（含装载时间和读取时间）
    double readFromDisc(const std::string& disc_id, uint64_t offset, uint64_t length);

    nlohmann::json to_json() const;
};
