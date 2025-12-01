#include "OpticalDiscLibrary.h"
#include "OpticalDisc.h"

#include <algorithm>  // std::find
#include <iostream>
#include <string>

OpticalDiscLibrary::OpticalDiscLibrary(std::string id,
                                       uint16_t disc_num,
                                       uint32_t drive_num,
                                       double load_time)
    : library_id(std::move(id)),
      disc_num(disc_num),
      drive_count(drive_num),
      load_unload_time(load_time) {}

// 添加一张新光盘
void OpticalDiscLibrary::addDisc(const std::string& disc_id) {
    if (disc_num > OPTICAL_LIBRARY_DISC_NUM) {
        std::cerr << "[OpticalDiscLibrary] 光盘数量超出库容量: " << disc_num << std::endl;
        return;
    }

    // 解析光盘 id，假定格式 "disc_0000100001"
    int id_num  = std::stoi(disc_id.substr(5));
    int lib_idx = id_num / OPTICAL_LIBRARY_DISC_NUM;
    int slot_idx = id_num % OPTICAL_LIBRARY_DISC_NUM;

    // 当前库号
    int cur_lib_idx = std::stoi(library_id.substr(4)); // 跳过 "lib_"

    if (lib_idx == cur_lib_idx) {
        // 若该槽在 miss 列表中，则移除 miss；否则视为槽已被“默认光盘”占用
        auto it = std::find(miss_slots.begin(), miss_slots.end(), slot_idx);
        if (it != miss_slots.end()) {
            miss_slots.erase(it);
        } else {
            // 槽已被占：如有 miss 槽，挪到第一个 miss 槽位作为“非默认光盘”
            if (!miss_slots.empty()) {
                int miss_slot = miss_slots.front();
                non_default_discs[miss_slot] = non_default_discs[slot_idx];
                miss_slots.erase(miss_slots.begin());
            }
        }
    } else {
        // 库号不一致：尝试放到 miss 槽作为“非默认光盘”
        if (!miss_slots.empty()) {
            int miss_slot = miss_slots.front();
            non_default_discs[miss_slot] = disc_id;
            miss_slots.erase(miss_slots.begin());
        } else {
            std::cerr << "[OpticalDiscLibrary] 没有空槽可放置光盘: " << disc_id << std::endl;
        }
    }
    ++disc_num;
}

// 按ID查找光盘（若存在则返回槽位）
int OpticalDiscLibrary::hasDisc(const std::string& disc_id) const {
    int id_num  = std::stoi(disc_id.substr(5));
    int lib_idx = id_num / OPTICAL_LIBRARY_DISC_NUM;
    int slot_idx = id_num % OPTICAL_LIBRARY_DISC_NUM;
    int cur_lib_idx = std::stoi(library_id.substr(4));

    if (lib_idx == cur_lib_idx) {
        // 检查槽是否 miss
        auto it = std::find(miss_slots.begin(), miss_slots.end(), slot_idx);
        if (it != miss_slots.end()) {
            // 槽为空，不包含
            return -1;
        }
        // 检查是否装了其他光盘
        auto nd_it = non_default_discs.find(slot_idx);
        if (nd_it != non_default_discs.end()) {
            // 槽装了其他光盘
            return -1;
        }
        // 默认光盘即存在
        return slot_idx;
    } else {
        // 库号不一致，查非默认光盘
        for (const auto& kv : non_default_discs) {
            if (kv.second == disc_id) return kv.first;
        }
        return -1;
    }
}

// 移除光盘，返回是否成功
bool OpticalDiscLibrary::removeDisc(const std::string& disc_id) {
    int slot_idx = hasDisc(disc_id);
    if (slot_idx < 0) {
        return false;
    }
    // 记录到 miss 槽
    miss_slots.push_back(slot_idx);
    // 若非默认映射中存在该槽，移除
    auto nd_it = non_default_discs.find(slot_idx);
    if (nd_it != non_default_discs.end()) {
        non_default_discs.erase(nd_it);
    }
    return true;
}

// 模拟刻录，返回总耗时（含装载时间和刻录时间）
double OpticalDiscLibrary::burnToDisc(const std::string& disc_id, uint64_t img_size) {
    // 需要库内能“定位到”该盘
    if (hasDisc(disc_id) < 0) return -1.0;

    // 容量约束（使用标准盘容量常量；此处不依赖 DiscManager/具体光盘对象）
    if (img_size > OPTICAL_DISC_CAPACITY) return -1.0;

    // 装载时间 + 写入时间（MB/s -> 字节/秒）
    double write_sec = static_cast<double>(img_size) / (OPTICAL_DISC_WRITE_MBPS * 1024.0 * 1024.0);
    return load_unload_time + write_sec;
}

// 模拟读取，返回总耗时（含装载时间和读取时间）
double OpticalDiscLibrary::readFromDisc(const std::string& disc_id, uint64_t offset, uint64_t length) {
    (void)offset; // 当前库层模型不区分偏移，保留参数避免未使用告警

    if (hasDisc(disc_id) < 0) return -1.0;
    if (length == 0) return load_unload_time; // 只发生装载

    double read_sec = static_cast<double>(length) / (OPTICAL_DISC_READ_MBPS * 1024.0 * 1024.0);
    return load_unload_time + read_sec;
}

// 序列化
nlohmann::json OpticalDiscLibrary::to_json() const {
    nlohmann::json j;
    j["library_id"]       = library_id;
    j["drive_count"]      = drive_count;
    j["disc_num"]         = disc_num;
    j["load_unload_time"] = load_unload_time;
    j["miss_slots"]       = miss_slots;
    j["non_default_discs"] = nlohmann::json::object();
    for (const auto& kv : non_default_discs) {
        j["non_default_discs"][std::to_string(kv.first)] = kv.second;
    }
    return j;
}
