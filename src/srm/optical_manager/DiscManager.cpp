#include "DiscManager.h"
#include "storagenode/optical/OpticalDisc.h"

#include <fstream>
#include <iostream>
#include <cstdio>    // snprintf
#include <algorithm>
#include <cstring>

void DiscManager::addDisc(const std::shared_ptr<OpticalDisc>& disc, DiscStatus status) {
    all_discs[std::string(disc->device_id)] = disc;
    setDiscStatus(disc, status);
    switch (status) {
        case DiscStatus::Blank:     blank_discs.insert(std::string(disc->device_id)); break;
        case DiscStatus::InUse:     inuse_discs.insert(std::string(disc->device_id)); break;
        case DiscStatus::Finalized: finalized_discs.insert(std::string(disc->device_id)); break;
        case DiscStatus::Recycled:  recycled_discs.insert(std::string(disc->device_id)); break;
        case DiscStatus::Lost:      lost_discs.insert(std::string(disc->device_id)); break;
    }
}

void DiscManager::setDiscStatus(const std::shared_ptr<OpticalDisc>& disc, DiscStatus status) {
    std::string id = std::string(disc->device_id);
    blank_discs.erase(id);
    inuse_discs.erase(id);
    finalized_discs.erase(id);
    recycled_discs.erase(id);
    lost_discs.erase(id);

    switch (status) {
        case DiscStatus::Blank:     blank_discs.insert(id); break;
        case DiscStatus::InUse:     inuse_discs.insert(id); break;
        case DiscStatus::Finalized: finalized_discs.insert(id); break;
        case DiscStatus::Recycled:  recycled_discs.insert(id); break;
        case DiscStatus::Lost:      lost_discs.insert(id); break;
    }
    // 同步状态到对象本身（可选）
    disc->status = status;
}

size_t DiscManager::totalDiscCount() const    { return all_discs.size(); }
size_t DiscManager::blankDiscCount() const    { return blank_discs.size(); }
size_t DiscManager::inuseDiscCount() const    { return inuse_discs.size(); }
size_t DiscManager::finalizedDiscCount() const{ return finalized_discs.size(); }
size_t DiscManager::recycledDiscCount() const { return recycled_discs.size(); }
size_t DiscManager::lostDiscCount() const     { return lost_discs.size(); }

std::shared_ptr<OpticalDisc> DiscManager::findDisc(const std::string& id) {
    // 解析 disc_xxxxxyyyyy：xxxxx 为批次，yyyyy 为索引
    size_t pos = id.find_last_of('_');
    if (pos == std::string::npos) return nullptr;
    std::string num_str = id.substr(pos + 1);
    if (num_str.empty()) return nullptr;

    int num = std::stoi(num_str);
    int batch_idx = num / 100000;
    int idx = num % 100000;

    std::cout << "查找光盘: " << id << "，批次: " << batch_idx << "，索引: " << idx << std::endl;

    size_t batch_pos = current_binary_file.find("disc_batch_");
    if (batch_pos == std::string::npos) return nullptr;

    // 从当前文件名提取当前批次号
    size_t start = batch_pos + std::strlen("disc_batch_");
    size_t dot   = current_binary_file.find(".bin", start);
    if (dot == std::string::npos) return nullptr;
    int current_idx = std::stoi(current_binary_file.substr(start, dot - start));
    std::cout << "当前批次索引: " << current_idx << std::endl;

    auto print_disc_info = [&](const std::shared_ptr<OpticalDisc>& disc, const std::string& file) {
        std::cout << "[DiscManager] 找到光盘: " << disc->device_id
                  << " 所在库: " << disc->library_id
                  << " 容量: "  << disc->capacity
                  << " 状态: "  << static_cast<int>(disc->status)
                  << " 写速: "  << disc->write_throughput_MBps
                  << " 读速: "  << disc->read_throughput_MBps
                  << " 文件: "  << file << std::endl;
    };

    if (batch_idx == current_idx) {
        if (idx >= 0 && static_cast<size_t>(idx) < cache_discs.size()) {
            if (id == cache_discs[idx]->device_id) {
                print_disc_info(cache_discs[idx], current_binary_file);
                return cache_discs[idx];
            }
        }
        return nullptr;
    } else {
        // 切换批次文件并加载
        saveCacheToBin();
        std::string target_file = current_binary_file;
        target_file.replace(start, dot - start, std::to_string(batch_idx));
        current_binary_file = target_file;

        loadCacheFromBin();
        std::cout << "加载光盘缓存成功: " << current_binary_file << std::endl;
        std::cout << "当前缓存光盘数量: " << cache_discs.size() << std::endl;

        if (idx >= 0 && static_cast<size_t>(idx) < cache_discs.size()) {
            std::cout << "查找光盘: " << id << " 索引: " << idx
                      << "，缓存对应: " << cache_discs[idx]->device_id << std::endl;
            if (id == cache_discs[idx]->device_id) {
                print_disc_info(cache_discs[idx], current_binary_file);
                return cache_discs[idx];
            }
        }
        return nullptr;
    }
}

void DiscManager::recycleDisc(const std::string& id) {
    auto it = all_discs.find(id);
    if (it != all_discs.end()) {
        setDiscStatus(it->second, DiscStatus::Recycled);
        return;
    }
    auto disc = findDisc(id);
    if (disc) setDiscStatus(disc, DiscStatus::Recycled);
}

void DiscManager::generateBlankDiscs(int count) {
    const int batch_size  = 100000;
    int batch_count       = (count + batch_size - 1) / batch_size; // ceil
    int written_total     = 0;

    for (int batch = 0; batch < batch_count; ++batch) {
        std::string filename = "/mnt/md0/node/disc/disc_batch_" + std::to_string(batch) + ".bin";
        std::ofstream ofs(filename, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            std::cerr << "[DiscManager] 无法打开文件: " << filename << std::endl;
            continue;
        }
        int discs_in_this_file = std::min(batch_size, count - written_total);
        for (int i = 0; i < discs_in_this_file; ++i, ++written_total) {
            char id_buf[32], lib_buf[16];
            std::snprintf(id_buf, sizeof(id_buf), "disc_%05d%05d", batch, i); // 10位补零
            int lib_idx = written_total / 20000; // 每 2 万光盘分配一个库
            std::snprintf(lib_buf, sizeof(lib_buf), "lib_%05d", lib_idx);

            // 注意 OpticalDisc 构造参数顺序：id, library_id, capacity, write_tp, read_tp
            OpticalDisc disc(id_buf, lib_buf, OPTICAL_DISC_CAPACITY,
                             OPTICAL_DISC_WRITE_MBPS, OPTICAL_DISC_READ_MBPS);

            ofs.write(reinterpret_cast<const char*>(&disc), sizeof(disc));
        }
        ofs.close();
    }
}

void DiscManager::saveCacheToBin() {
    if (current_binary_file.empty()) return;
    std::ofstream ofs(current_binary_file, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "[DiscManager] 无法写入文件: " << current_binary_file << std::endl;
        return;
    }
    for (const auto& disc : cache_discs) {
        ofs.write(reinterpret_cast<const char*>(disc.get()), sizeof(OpticalDisc));
    }
    ofs.close();
}

// 读取文件到 cache_discs
void DiscManager::loadCacheFromBin() {
    cache_discs.clear();
    std::ifstream ifs(current_binary_file, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[DiscManager] 无法打开文件: " << current_binary_file << std::endl;
        return;
    }
    while (true) {
        OpticalDisc disc("", "", 0, 0.0, 0.0);
        ifs.read(reinterpret_cast<char*>(&disc), sizeof(disc));
        if (static_cast<size_t>(ifs.gcount()) != sizeof(disc)) break;

        auto ptr = std::make_shared<OpticalDisc>(
            std::string(disc.device_id),
            std::string(disc.library_id),
            disc.capacity,
            disc.write_throughput_MBps,
            disc.read_throughput_MBps
        );
        ptr->status = disc.status;
        cache_discs.push_back(ptr);
    }
    ifs.close();
    std::cout << "[DiscManager] 已加载光盘缓存: " << current_binary_file
              << "，数量: " << cache_discs.size() << std::endl;
}
