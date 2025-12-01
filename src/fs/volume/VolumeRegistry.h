#pragma once
#include <memory>
#include <vector>
#include <string>
#include "Volume.h"

enum class VolumeType { SSD, HDD };

class IVolumeRegistry {
public:
    virtual ~IVolumeRegistry() = default;

    /**
     * @brief 注册卷并可选立即持久化。
     * @param vol 待注册的卷实例。
     * @param type 卷类型（SSD 或 HDD）。
     * @param out_index 若不为空，则返回持久化条目的索引。
     * @param persist_now 是否立即写入持久化存储。
     * @return 注册是否成功。
     */
    virtual bool register_volume(const std::shared_ptr<Volume>& vol,
                                 VolumeType type,
                                 int* out_index = nullptr,
                                 bool persist_now = false) = 0;

    /**
     * @brief 通过 UUID 查找卷。
     * @param uuid 目标卷的唯一标识。
     * @return 找到的卷指针，未命中返回 nullptr。
     */
    virtual std::shared_ptr<Volume> find_by_uuid(const std::string& uuid) = 0;

    /**
     * @brief 列出指定类型的所有卷。
     * @param type 卷类型。
     * @return 指向对应卷列表的常量引用。
     */
    virtual const std::vector<std::shared_ptr<Volume>>& list(VolumeType type) const = 0;

    /**
     * @brief 启动时加载卷信息。
     * @return 启动流程是否成功。
     */
    virtual bool startup() = 0;

    /**
     * @brief 关闭时持久化卷信息。
     * @return 关停流程是否成功。
     */
    virtual bool shutdown() = 0;
};

// 工厂：创建基于文件的 VolumeRegistry 实现（在 VolumeRegistry.cpp 中定义）
std::shared_ptr<IVolumeRegistry> make_file_volume_registry(const std::string& base_dir = ".");