#include "StorageNode.h"
#include "SSD.h"
#include "HDD.h"
#include "StorageTypes.h"     // SSD/HDD 默认容量与吞吐、块大小宏
#include "fs/volume/Volume.h"
#include "fs/block/BlockManager.h"        // 使用 BLOCK_SIZE
#include "msg/IO.h"
#include <iostream>
#include <memory>
#include <string>
#include <utility>

void StorageNode::addDevice(const std::shared_ptr<Device>& dev) {
    if (dev->type() == "SolidStateDrive") {
        ssd_devices.push_back(dev);
        ssd_device_count = ssd_devices.size();
    } else if (dev->type() == "HardDiskDrive") {
        hdd_devices.push_back(dev);
        hdd_device_count = hdd_devices.size();
    }
    // 其他类型可扩展
}

StorageNode::StorageNode(std::string id, StorageNodeType t)
    : node_id(std::move(id)), type(t) {
    // 根据类型自动设置设备数量
    switch (type) {
        case StorageNodeType::SSD: {
            ssd_device_count = 4; // 例如每个SSD节点有4块SSD
            uint64_t total_ssd_capacity = 0;
            for (size_t i = 0; i < ssd_device_count; ++i) {
                auto dev = std::make_shared<SolidStateDrive>(
                    node_id + "_SSD_" + std::to_string(i),
                    SSD_DEFAULT_CAPACITY,
                    SSD_DEFAULT_WRITE_MBPS,
                    SSD_DEFAULT_READ_MBPS
                );
                ssd_devices.push_back(dev);
                total_ssd_capacity += SSD_DEFAULT_CAPACITY;
            }
            // 初始化卷，在外部调用 initVolumes()
            break;
        }
        case StorageNodeType::HDD: {
            hdd_device_count = 8; // 例如每个HDD节点有8块HDD
            uint64_t total_hdd_capacity = 0;
            for (size_t i = 0; i < hdd_device_count; ++i) {
                auto dev = std::make_shared<HardDiskDrive>(
                    node_id + "_HDD_" + std::to_string(i),
                    HDD_DEFAULT_CAPACITY,
                    HDD_DEFAULT_WRITE_MBPS,
                    HDD_DEFAULT_READ_MBPS
                );
                hdd_devices.push_back(dev);
                total_hdd_capacity += HDD_DEFAULT_CAPACITY;
            }
            // 初始化卷，在外部调用 initVolumes()
            break;
        }
        case StorageNodeType::Mix: {
            size_t ssd_count = 3, hdd_count = 3; // 混合存储节点，3块SSD和3块HDD
            uint64_t total_ssd_capacity = 0, total_hdd_capacity = 0;
            for (size_t i = 0; i < ssd_count; ++i) {
                auto dev = std::make_shared<SolidStateDrive>(
                    node_id + "_SSD_" + std::to_string(i),
                    SSD_DEFAULT_CAPACITY,
                    SSD_DEFAULT_WRITE_MBPS,
                    SSD_DEFAULT_READ_MBPS
                );
                ssd_devices.push_back(dev);
                total_ssd_capacity += SSD_DEFAULT_CAPACITY;
            }
            for (size_t i = 0; i < hdd_count; ++i) {
                auto dev = std::make_shared<HardDiskDrive>(
                    node_id + "_HDD_" + std::to_string(i),
                    HDD_DEFAULT_CAPACITY,
                    HDD_DEFAULT_WRITE_MBPS,
                    HDD_DEFAULT_READ_MBPS
                );
                hdd_devices.push_back(dev);
                total_hdd_capacity += HDD_DEFAULT_CAPACITY;
            }
            ssd_device_count = ssd_count;
            hdd_device_count = hdd_count;
            // 初始化卷，在外部调用 initVolumes()
            break;
        }
    }
}

void StorageNode::initVolumes() {
    // SSD卷
    if (!ssd_devices.empty()) {
        uint64_t total_ssd_capacity = 0;
        for (const auto& dev : ssd_devices) total_ssd_capacity += dev->capacity;

        ssd_volume = std::make_shared<Volume>(
            node_id + "_SSD_VOL", node_id,
            total_ssd_capacity / SSD_BLOCK_SIZE,
            SSD_BLOCK_SIZE,
            SSD_BLOCKS_PER_GROUP
        );
    } else {
        ssd_volume.reset();
    }

    // HDD卷
    if (!hdd_devices.empty()) {
        uint64_t total_hdd_capacity = 0;
        for (const auto& dev : hdd_devices) total_hdd_capacity += dev->capacity;

        hdd_volume = std::make_shared<Volume>(
            node_id + "_HDD_VOL", node_id,
            total_hdd_capacity / HDD_BLOCK_SIZE,
            HDD_BLOCK_SIZE,
            HDD_BLOCKS_PER_GROUP
        );
    } else {
        hdd_volume.reset();
    }
    volume_initialized = true;
}

double StorageNode::processIO(const IORequest& req) {
    // 检查存储节点ID是否一致
    if (req.storage_node_id != node_id) {
        std::cout << "[StorageNode] IORequest存储节点ID不匹配: "
                  << req.storage_node_id << " != " << node_id << std::endl;
        return -1.0;
    }

    size_t device_idx = req.start_block; // 用于分配设备（当前逻辑未使用）

    // 固定选择 SSD 第 0 块设备
    std::shared_ptr<Device> target_device = nullptr;
    if (ssd_devices.empty()) {
        std::cout << "[StorageNode] 无SSD设备可用" << std::endl;
        return -1.0;
    }
    target_device = ssd_devices[0];

    // 计算偏移和长度（使用 BLOCK_SIZE）
    uint64_t offset = static_cast<uint64_t>(req.start_block) * BLOCK_SIZE;
    uint64_t length = static_cast<uint64_t>(req.block_count) * BLOCK_SIZE;

    // 调用设备的读写方法
    if (req.type == IOType::Read) {
        return target_device->read(offset, length);
    } else if (req.type == IOType::Write) {
        return target_device->write(offset, length);
    } else {
        std::cout << "[StorageNode] 不支持的IO类型: " << static_cast<int>(req.type) << std::endl;
        return -1.0;
    }
}
