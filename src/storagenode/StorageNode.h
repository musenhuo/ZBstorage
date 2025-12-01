#pragma once
#include "StorageTypes.h"
#include "hard_disc/Device.h"
#include <memory>
#include <vector>
#include <string>
#include "msg/IO.h"   // IORequest / IOType 定义

// 前向声明，避免在头文件中引入具体实现
class Volume;

class StorageNode {
public:
    std::string node_id;                        // 存储节点ID
    StorageNodeType type;                       // 存储节点类型
    size_t ssd_device_count;                    // 设备数量
    size_t hdd_device_count;                    // 设备数量
    bool volume_initialized = false;          // 卷空间是否已初始化
    // 分别存放SSD和HDD设备
    std::vector<std::shared_ptr<Device>> ssd_devices;
    std::vector<std::shared_ptr<Device>> hdd_devices;

    // 卷空间：改为 shared_ptr，便于外部持有引用
    std::shared_ptr<Volume> ssd_volume;         // 仅SSD或混合节点的SSD卷
    std::shared_ptr<Volume> hdd_volume;         // 仅HDD或混合节点的HDD卷

    // 无参构造函数
        StorageNode()
                : node_id(""), type(StorageNodeType::SSD), ssd_device_count(0), hdd_device_count(0),
                    ssd_volume(nullptr), hdd_volume(nullptr) {}

    // 构造函数：根据类型自动设置设备数量并初始化设备
    StorageNode(std::string id, StorageNodeType t);

    void addDevice(const std::shared_ptr<Device>& dev);

    // 根据当前设备容器初始化卷空间
    void initVolumes();

    // 读写方法，输入IORequest，返回性能模型（耗时，单位秒）
    double processIO(const IORequest& req);
};
