#include "StorageResource.h"
#include "storagenode/StorageNode.h"
#include "storagenode/hard_disc/SSD.h"
#include "storagenode/hard_disc/HDD.h"
#include "storagenode/optical/OpticalDiscLibrary.h"
#include "fs/volume/Volume.h"
#include "msg/IO.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <chrono>
#include <climits>
#include <memory>
#include <utility>
#include <algorithm>
#include <cstdlib>

// -------------------------------------------------------------
// StorageResource 成员函数实现（无 ZBLog 依赖）
// -------------------------------------------------------------

void StorageResource::addNode(const std::shared_ptr<StorageNode>& node) {
    if (!node) return;
    // 默认将新节点加入未初始化集合；是否已初始化可通过卷指针判断
    uninitialized_nodes.push_back(node);
    uninit_sorted_ = false; // 新插入后需重新排序
}

std::shared_ptr<StorageNode> StorageResource::findNode(const std::string& node_id) const {
    for (const auto& node : nodes) {
        if (node->node_id == node_id) return node;
    }
    for (const auto& node : uninitialized_nodes) {
        if (node->node_id == node_id) return node;
    }
    return nullptr;
}

std::pair<std::shared_ptr<Volume>, std::shared_ptr<Volume>>
StorageResource::initOneNodeVolume() {
    if (uninitialized_nodes.empty()) {
        std::cerr << "[StorageResource] 没有未初始化的存储节点可用!" << std::endl;
        return {nullptr, nullptr};
    }
    // 确保按 id 排序
    if (!uninit_sorted_) {
        sortUninitializedById();
    }
    if (uninit_cursor_ >= uninitialized_nodes.size()) {
        std::cerr << "[StorageResource] 未初始化节点已全部处理完毕!" << std::endl;
        return {nullptr, nullptr};
    }
    auto node = uninitialized_nodes[uninit_cursor_++];
    node->initVolumes();
    nodes.push_back(node);
    return {node->ssd_volume, node->hdd_volume};
}

double StorageResource::processIO(const IORequest& req) {
    if (req.storage_node_id == "node_mix_99" || req.storage_node_id == "node_ssd_99") {
        size_t block_size = 4096; // 假设块大小
        size_t file_offset = req.start_block * block_size + req.offset_in_block;
        size_t io_size = req.data_size > 0 ? req.data_size : req.block_count * block_size;
        // 构造文件路径
        std::string file_path = "/mnt/md0/node/" + req.storage_node_id + "_" + req.volume_id + ".txt";

        if (req.type == IOType::Read) {
            FILE* fp = std::fopen(file_path.c_str(), "r");
            if (!fp) return -1.0;
            std::fseek(fp, static_cast<long>(file_offset), SEEK_SET);
            // 直接读到 req.buffer
            size_t readed = std::fread(req.buffer, 1, io_size, fp);
            std::fclose(fp);
            ((IORequest&)req).buffer_size = readed;

            if (req.buffer && readed > 0) {
                std::string out(static_cast<char*>(req.buffer), ((IORequest&)req).buffer_size);
                (void)out; // 需要时可打印
            }
        } else if (req.type == IOType::Write) {
            FILE* fp = std::fopen(file_path.c_str(), "r+");
            if (!fp) {
                // 文件不存在则创建
                fp = std::fopen(file_path.c_str(), "w+");
            }
            if (!fp) return -1.0;
            std::fseek(fp, static_cast<long>(file_offset), SEEK_SET);
            // 写入 req.buffer 内容
            if (req.buffer && req.buffer_size > 0) {
                std::fwrite(req.buffer, 1, req.buffer_size, fp);
            }
            std::fclose(fp);
        } else if (req.type == IOType::Delete) {
            std::remove(file_path.c_str());
        }
    }

    auto node = findNode(req.storage_node_id);
    if (node) {
        std::cout << "[StorageResource] 调用节点 " << node->node_id << " 处理IO请求..." << std::endl;
        double result = node->processIO(req);
        std::cout << "[StorageResource] 处理结果: " << result << " 秒" << std::endl;
        return result;
    }
    std::cerr << "[StorageResource] 未找到节点: " << req.storage_node_id << std::endl;
    return -1.0;
}

void StorageResource::generateResource() {
    using namespace std::chrono;
    std::cout << "[StorageResource] 开始生成存储资源..." << std::endl;
    auto start_nodes = steady_clock::now();

    // 生成存储节点（未初始化卷）
    uninitialized_nodes.reserve(10000 * 3);
    for (int i = 0; i < 10000; ++i) {
        uninitialized_nodes.push_back(
            std::make_shared<StorageNode>("ssd_node_" + std::to_string(i), StorageNodeType::SSD));
        uninitialized_nodes.push_back(
            std::make_shared<StorageNode>("hdd_node_" + std::to_string(i), StorageNodeType::HDD));
        uninitialized_nodes.push_back(
            std::make_shared<StorageNode>("mix_node_" + std::to_string(i), StorageNodeType::Mix));
        std::cout << "[进度] 已生成存储节点: " << i << " / 10000" << std::endl;
    }
    sortUninitializedById();
    auto end_nodes = steady_clock::now();
    std::cout << "[StorageResource] 存储节点生成完成，用时: "
              << duration_cast<seconds>(end_nodes - start_nodes).count() << " 秒" << std::endl;

    auto start_libs = steady_clock::now();
    // 生成光盘库
    for (int lib_idx = 0; lib_idx < 50000; ++lib_idx) {
        char lib_id[16];
        std::snprintf(lib_id, sizeof(lib_id), "lib_%05d", lib_idx);
        auto lib = std::make_shared<OpticalDiscLibrary>(lib_id);
        libraries.push_back(lib);
        if ((lib_idx + 1) % 1000 == 0) {
            std::cout << "[进度] 已生成光盘库: " << (lib_idx + 1) << " / 50000" << std::endl;
        }
    }
    auto end_libs = steady_clock::now();
    std::cout << "[StorageResource] 光盘库生成完成，用时: "
              << duration_cast<seconds>(end_libs - start_libs).count() << " 秒" << std::endl;
}

void StorageResource::printInfo() const {
    std::cout << "未初始化卷空间存储节点数量: " << uninitialized_nodes.size() << std::endl;
    std::cout << "已初始化卷空间存储节点数量: " << nodes.size() << std::endl;
    std::cout << "光盘库数量: " << libraries.size() << std::endl;
}

void StorageResource::saveToFile(const std::string& filename1,
                                 const std::string& filename2) const {
    using namespace std::chrono;
    nlohmann::json j;
    std::cout << "[StorageResource] 开始保存存储节点..." << std::endl;
    auto start_nodes = steady_clock::now();

    j["nodes"] = nlohmann::json::array();

    auto dump_node = [&](const std::shared_ptr<StorageNode>& node) {
        nlohmann::json node_j;
        node_j["node_id"]            = node->node_id;
        node_j["type"]               = static_cast<int>(node->type);
    // 用卷指针是否存在来推断初始化状态
    bool initialized = (node->ssd_volume != nullptr) || (node->hdd_volume != nullptr);
    node_j["volume_initialized"] = initialized;

        // 保存 SSD 设备
        node_j["ssd_devices"] = nlohmann::json::array();
        for (const auto& dev : node->ssd_devices) {
            node_j["ssd_devices"].push_back(dev->to_json());
        }
        node_j["ssd_device_count"] = node->ssd_device_count;

        // 保存 HDD 设备
        node_j["hdd_devices"] = nlohmann::json::array();
        for (const auto& dev : node->hdd_devices) {
            node_j["hdd_devices"].push_back(dev->to_json());
        }
        node_j["hdd_device_count"] = node->hdd_device_count;

        j["nodes"].push_back(node_j);
    };

    for (const auto& node : nodes) dump_node(node);
    // 只保存未处理的未初始化节点（从游标 uninit_cursor_ 开始），避免重复保存已初始化的节点
    for (size_t i = uninit_cursor_; i < uninitialized_nodes.size(); ++i) {
        dump_node(uninitialized_nodes[i]);
    }

    std::ofstream ofs1(filename1);
    ofs1 << j.dump(4);
    ofs1.close();

    auto end_nodes = steady_clock::now();
    std::cout << "[StorageResource] 存储节点保存完成，用时: "
              << duration_cast<seconds>(end_nodes - start_nodes).count() << " 秒" << std::endl;

    // 保存光盘库
    nlohmann::json j2;
    std::cout << "[StorageResource] 开始保存光盘库..." << std::endl;
    auto start_libs = steady_clock::now();

    j2["libraries"] = nlohmann::json::array();
    for (const auto& lib : libraries) {
        j2["libraries"].push_back(lib->to_json());
    }
    std::ofstream ofs2(filename2);
    ofs2 << j2.dump(4);
    ofs2.close();

    auto end_libs = steady_clock::now();
    std::cout << "[StorageResource] 光盘库保存完成，用时: "
              << duration_cast<seconds>(end_libs - start_libs).count() << " 秒" << std::endl;
}

void StorageResource::loadFromFile(bool initvolumes,
                                   bool fresh,
                                   const std::string& filename1,
                                   const std::string& filename2) {
    using namespace std::chrono;

    // ------- 读取节点 -------
    auto start_nodes = steady_clock::now();
    nodes.clear();
    uninitialized_nodes.clear();
    uninit_cursor_ = 0;
    uninit_sorted_ = false;

    std::ifstream ifs(filename1);
    nlohmann::json j;
    ifs >> j;

    size_t node_count = 0;
    for (const auto& node_j : j["nodes"]) {
        auto node = std::make_shared<StorageNode>(); // 用无参构造
        node->node_id = node_j["node_id"].get<std::string>();
        node->type    = static_cast<StorageNodeType>(node_j["type"].get<int>());

        // SSD
        if (node_j.contains("ssd_devices")) {
            for (const auto& dev_j : node_j["ssd_devices"]) {
                std::string dev_type = dev_j["type"];
                if (dev_type == "SolidStateDrive") {
                    auto dev = std::make_shared<SolidStateDrive>(
                        dev_j["device_id"],
                        dev_j["capacity"],
                        dev_j.value("write_throughput_MBps", SSD_DEFAULT_WRITE_MBPS),
                        dev_j.value("read_throughput_MBps",  SSD_DEFAULT_READ_MBPS)
                    );
                    node->addDevice(dev);
                }
            }
        }

        // HDD
        if (node_j.contains("hdd_devices")) {
            for (const auto& dev_j : node_j["hdd_devices"]) {
                std::string dev_type = dev_j["type"];
                if (dev_type == "HardDiskDrive") {
                    auto dev = std::make_shared<HardDiskDrive>(
                        dev_j["device_id"],
                        dev_j["capacity"],
                        dev_j.value("write_throughput_MBps", HDD_DEFAULT_WRITE_MBPS),
                        dev_j.value("read_throughput_MBps",  HDD_DEFAULT_READ_MBPS)
                    );
                    node->addDevice(dev);
                }
            }
        }

        if (initvolumes) {
            node->initVolumes();
            nodes.push_back(node);
        } else if (fresh) {
            uninitialized_nodes.push_back(node);
        } else {
            // 默认作为未初始化，后续按需初始化
            uninitialized_nodes.push_back(node);
        }

        ++node_count;
        if (node_count % 2000 == 0) {
            std::cout << "[StorageResource] 已加载节点 " << node_count << std::endl;
        }
    }
    // 文件加载完之后统一排序
    std::cout << "[StorageResource] 节点加载完成，准备排序 (总数: "
              << node_count << ")" << std::endl;
    sortUninitializedById();
    auto end_nodes = steady_clock::now();
    std::cout << "[StorageResource] 存储节点读取完成，用时: "
              << duration_cast<seconds>(end_nodes - start_nodes).count() << " 秒" << std::endl;

    // ------- 读取光盘库 -------
    auto start_libs = steady_clock::now();
    libraries.clear();

    std::ifstream ifs2(filename2);
    nlohmann::json j2;
    ifs2 >> j2;

    size_t lib_count = 0;
    for (const auto& lib_j : j2["libraries"]) {
        std::string library_id       = lib_j.value("library_id", "");
        uint16_t    disc_num         = lib_j.value("disc_num", 0);
        uint32_t    drive_count      = lib_j.value("drive_count", 0u);
        double      load_unload_time = lib_j.value("load_unload_time", 0.0);

        auto lib = std::make_shared<OpticalDiscLibrary>(
            library_id, disc_num, drive_count, load_unload_time);

        if (lib_j.contains("miss_slots") && lib_j["miss_slots"].is_array()) {
            for (const auto& slot : lib_j["miss_slots"]) {
                lib->miss_slots.push_back(slot.get<int>());
            }
        }
        if (lib_j.contains("non_default_discs") && lib_j["non_default_discs"].is_object()) {
            for (auto it = lib_j["non_default_discs"].begin();
                 it != lib_j["non_default_discs"].end(); ++it) {
                int         slot_idx = std::stoi(it.key());
                std::string disc_id  = it.value();
                lib->non_default_discs[slot_idx] = disc_id;
            }
        }
        libraries.push_back(lib);
        ++lib_count;
        if (lib_count % 1000 == 0) {
            // 可选：打印进度
        }
    }
    auto end_libs = steady_clock::now();
    std::cout << "[StorageResource] 光盘库读取完成，用时: "
              << duration_cast<seconds>(end_libs - start_libs).count() << " 秒" << std::endl;
}

// ---------------- 内部辅助：按 id 排序与 rank 计算 ----------------

long StorageResource::parseIdSuffixNumber(const std::string& node_id) {
    // 解析最后一个 '_' 后的数字部分，如 "ssd_node_123" -> 123
    auto pos = node_id.find_last_of('_');
    if (pos == std::string::npos || pos + 1 >= node_id.size()) return -1;
    const char* p = node_id.c_str() + pos + 1;
    char* endp = nullptr;
    long v = std::strtol(p, &endp, 10);
    if (endp == p) return -1; // 没有数字
    return v;
}

long long StorageResource::nodeRank(const std::shared_ptr<StorageNode>& node) {
    // 类型权重：SSD=0, HDD=1, Mix=2，确保同一编号下 SSD 优先，其次 HDD，再次 Mix
    int type_weight = 0;
    switch (node->type) {
        case StorageNodeType::SSD: type_weight = 0; break;
        case StorageNodeType::HDD: type_weight = 1; break;
        case StorageNodeType::Mix: type_weight = 2; break;
    }
    long suffix = parseIdSuffixNumber(node->node_id);
    if (suffix < 0) suffix = LONG_MAX / 4; // 异常 id 放到后面
    // 组合 rank：高位是编号，低位是类型权重
    return (static_cast<long long>(suffix) << 3) | static_cast<long long>(type_weight);
}

void StorageResource::sortUninitializedById() {
    std::sort(uninitialized_nodes.begin(), uninitialized_nodes.end(),
        [&](const std::shared_ptr<StorageNode>& a, const std::shared_ptr<StorageNode>& b){
            auto ra = nodeRank(a);
            auto rb = nodeRank(b);
            if (ra != rb) return ra < rb;
            return a->node_id < b->node_id; // 次要规则，确保稳定
        }
    );
    uninit_cursor_ = 0;
    uninit_sorted_ = true;
}
