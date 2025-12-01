#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../mds/inode/inode.h"
#include "Volume.h"
#include "../io/IIOGateway.h"
#include "../../msg/IO.h"
#include "./Volume.h"
#include "./VolumeRegistry.h"

/**
 * @brief VolumeManager 聚合卷与 I/O 网关，负责块分配、I/ORequest 打包与下发。
 */
class VolumeManager {
private:
    struct VolumeContext {
        std::shared_ptr<Volume> volume;
        std::shared_ptr<IIOGateway> gateway;
    };

    std::unordered_map<std::string, VolumeContext> volumes_;
    std::shared_ptr<IIOGateway> default_gateway_;

    VolumeContext* resolve_context(const std::string& volume_uuid);
    bool ensure_blocks(const std::shared_ptr<Inode>& inode,
                       Volume& volume,
                       size_t total_blocks_needed,
                       size_t& bytes_allocated);
    void dispatch_requests(VolumeContext& ctx,
                           std::vector<IORequest>& requests);

public:
    /**
     * @brief 注册卷并可选绑定 I/O 网关。
     * @param volume 目标卷（必须有效且具备 uuid）。
     * @param gateway 该卷专用网关；可为空，表示使用默认网关。
     */
    void register_volume(std::shared_ptr<Volume> volume,
                         std::shared_ptr<IIOGateway> gateway = nullptr);

    /**
     * @brief 为指定卷设置/替换 I/O 网关。
     * @param volume_uuid 卷 uuid。
     * @param gateway 新网关实例。
     * @return 是否设置成功（卷必须已注册）。
     */
    bool set_volume_gateway(const std::string& volume_uuid,
                            std::shared_ptr<IIOGateway> gateway);

    /**
     * @brief 设置缺省 I/O 网关，供未绑定专用网关的卷使用。
     * @param gateway 网关实例。
     */
    void set_default_gateway(std::shared_ptr<IIOGateway> gateway);

    /**
     * @brief 按 inode 的卷信息执行写入。
     * @param inode 目标 inode（需包含 volume_id、block_segments 等信息）。
     * @param offset 文件内写偏移。
     * @param buf 数据指针。
     * @param count 写入字节数。
     * @return 实际写入字节；失败返回 -1。
     */
    ssize_t write_file(const std::shared_ptr<Inode>& inode,
                       size_t offset,
                       const char* buf,
                       size_t count);

    /**
     * @brief 按 inode 的卷信息执行读取。
     * @param inode 目标 inode。
     * @param offset 文件内读偏移。
     * @param buf 输出缓冲区。
     * @param count 读取字节数。
     * @return 实际读取字节；失败返回 -1。
     */
    ssize_t read_file(const std::shared_ptr<Inode>& inode,
                      size_t offset,
                      char* buf,
                      size_t count);

    /**
     * @brief 释放 inode 关联的全部块段并重置其块列表。
     * @param inode 目标 inode。
     * @return 若成功释放至少一个块段返回 true。
     */
    bool release_inode_blocks(const std::shared_ptr<Inode>& inode);
};