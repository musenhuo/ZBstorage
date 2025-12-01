#pragma once
#include <memory>
#include <string>
#include <vector>
#include <shared_mutex>
#include <unordered_map>
#include <boost/dynamic_bitset.hpp>
#include "../inode/inode.h"
#include "../metadataserver/MetadataManager.h"
#include "DirStore.h"
#include "DirectoryLockTable.h"
#include "../../fs/volume/VolumeRegistry.h"
#include "../../fs/volume/VolumeManager.h"
#include "../allocator/VolumeAllocator.h"

/**
 * @brief 句柄观察者接口
 *
 * MDS 在需要通知客户端关闭/回收与某个 inode 相关的打开句柄时，
 * 会调用此接口的方法。典型场景为：MDS 已释放或即将释放文件的
 * 物理块，需确保客户端不再通过旧的文件描述符访问被回收的数据。
 */
class IHandleObserver {
public:
    virtual ~IHandleObserver() = default;

    /**
     * @brief 请求观察者关闭与指定 inode 相关的所有本地句柄。
     *
     * @param inode 要关闭句柄的 inode 号。
     * @note 实现者应在本地找到并关闭/清理对应的 fd/句柄，保证
     *       后续 I/O 不会访问已回收的块。此调用由 MDS 在回收块后触发。
     */
    virtual void CloseHandlesForInode(uint64_t inode) = 0;
};

class MdsServer {
private:
    std::unique_ptr<MetadataManager> meta_;
    std::unique_ptr<DirStore> dir_store_;
    std::unordered_map<std::string, uint64_t> inode_table_;
    mutable std::shared_mutex mtx_namespace_;
    mds::DirectoryLockTable dir_lock_table_;
    // 可选的卷注册/分配组件
    std::shared_ptr<IVolumeRegistry> volume_registry_;
    std::unique_ptr<VolumeAllocator> volume_allocator_;
    std::shared_ptr<VolumeManager> volume_manager_;
    std::weak_ptr<IHandleObserver> handle_observer_;

    /**
     * @brief 私有：通知已注册的句柄观察者关闭 inode 关联句柄。
     *
     * @param inode 要通知的 inode 号。
     *
     * 此方法会尝试 lock() 弱引用的观察者并调用其 CloseHandlesForInode。
     */
    void notify_handle_observer(uint64_t inode);
    
public:
    /**
     * @brief 初始化 MDS 服务并可选择重建存储。
     * @param create_new 为 true 时创建全新存储。
     */
    explicit MdsServer(bool create_new = false);

    /**
     * @brief 指定路径初始化 MDS 服务。
     * @param inode_path inode 存储文件路径。
     * @param bitmap_path 位图文件路径。
     * @param dir_store_base 目录存储根路径。
     * @param create_new 是否创建全新存储。
     */
    MdsServer(const std::string& inode_path,
              const std::string& bitmap_path,
              const std::string& dir_store_base,
              bool create_new);

    /**
     * @brief 创建根目录。
     * @return 成功返回 true。
     */
    bool CreateRoot();

    /**
     * @brief 创建目录。
     * @param path 绝对路径。
     * @param mode 权限。
     * @return 成功返回 true。
     */
    bool Mkdir(const std::string& path, mode_t mode);

    /**
     * @brief 删除目录。
     * @param path 绝对路径。
     * @return 成功返回 true。
     */
    bool Rmdir(const std::string& path);

    /**
     * @brief 创建文件。
     * @param path 绝对路径。
     * @param mode 权限。
     * @return 成功返回 true。
     */
    bool CreateFile(const std::string& path, mode_t mode);

    /**
     * @brief 删除文件。
     * @param path 绝对路径。
     * @return 成功返回 true。
     */
    bool RemoveFile(const std::string& path);

    /**
     * @brief 截断文件至零长度。
     * @param path 绝对路径。
     * @return 成功返回 true。
     */
    bool TruncateFile(const std::string& path);

    /**
     * @brief 列目录内容。
     * @param path 绝对路径。
     * @return 成功返回 true。
     */
    bool Ls(const std::string& path);

    /**
     * @brief 查找路径对应 inode 号。
     * @param abs_path 绝对路径。
     * @return 找到返回 inode 号，失败返回 -1。
     */
    uint64_t LookupIno(const std::string& abs_path);

    /**
     * @brief 根据路径获取 inode。
     * @param path 绝对路径。
     * @return 找到返回 inode 指针，否则 nullptr。
     */
    std::shared_ptr<Inode> FindInodeByPath(const std::string& path);

    /**
     * @brief 往目录 inode 添加目录项。
     * @param dir_inode 目录 inode。
     * @param new_entry 目录项。
     * @return 成功返回 true。
     */
    bool AddDirectoryEntry(const std::shared_ptr<Inode>& dir_inode, const DirectoryEntry& new_entry);

    /**
     * @brief 从目录 inode 中移除目录项。
     * @param dir_inode 目录 inode。
     * @param name 目录项名称。
     * @return 成功返回 true。
     */
    bool RemoveDirectoryEntry(const std::shared_ptr<Inode>& dir_inode, const std::string& name);

    /**
     * @brief 读取目录项列表。
     * @param dir_inode 目录 inode。
     * @return 目录项数组。
     */
    std::vector<DirectoryEntry> ReadDirectoryEntries(const std::shared_ptr<Inode>& dir_inode);

    /**
     * @brief 获取根 inode 号。
     * @return 根 inode。
     */
    uint64_t GetRootInode() const;

    /**
     * @brief 获取总 inode 数。
     * @return 总 inode 槽位数。
     */
    uint64_t GetTotalInodes() const;

    /**
     * @brief 注入卷注册中心，供 MDS 在创建/删除文件时进行卷分配与块回收。
     */
    void set_volume_registry(std::shared_ptr<IVolumeRegistry> registry);

    /**
     * @brief 获取当前注入的卷注册中心（可能为 nullptr）。
     */
    std::shared_ptr<IVolumeRegistry> volume_registry() const;

    /**
     * @brief 注册卷（持久化由后端实现负责）。
     */
    bool RegisterVolume(const std::shared_ptr<Volume>& vol,
                        VolumeType type,
                        int* out_index = nullptr,
                        bool persist_now = false);

    /**
     * @brief 判断 inode 是否已分配。
     * @param ino inode 号。
     * @return 已分配返回 true。
     */
    bool IsInodeAllocated(uint64_t ino);

    /**
     * @brief 分配新 inode。
     * @param mode 目标类型权限。
     * @return 新 inode 号，失败返回 -1。
     */
    uint64_t AllocateInode(mode_t mode);

    /**
     * @brief 读取 inode 内容。
     * @param ino inode 号。
     * @param out 输出 inode。
     * @return 成功返回 true。
     */
    bool ReadInode(uint64_t ino, Inode& out);

    /**
     * @brief 写回 inode。
     * @param ino inode 号。
     * @param in 输入 inode。
     * @return 成功返回 true。
     */
    bool WriteInode(uint64_t ino, const Inode& in);

    /**
     * @brief 根据冷热标准收集冷 inode。
     * @param max_candidates 最大数量。
     * @param min_age_windows 最小冷却窗口。
     * @return inode 列表。
     */
    std::vector<uint64_t> CollectColdInodes(size_t max_candidates, size_t min_age_windows);

    /**
     * @brief 按位图返回冷 inode。
     * @param min_age_windows 最小冷却窗口。
     * @return 冷 inode 位图。
     */
    std::shared_ptr<boost::dynamic_bitset<>> CollectColdInodesBitmap(size_t min_age_windows);

    /**
     * @brief 按访问时间百分位收集冷 inode。
     * @param percent 百分位。
     * @return inode 列表。
     */
    std::vector<uint64_t> CollectColdInodesByAtimePercent(double percent);

    /**
     * @brief 重建路径→inode 映射表。
     */
    void RebuildInodeTable();

    /**
     * @brief 清空路径→inode 映射缓存。
     */
    void ClearInodeTable();

    /**
     * @brief 注入数据平面卷管理器（VolumeManager）。
     *
     * MDS 在执行删除/截断等操作时，会优先尝试通过注入的
     * `VolumeManager` 执行本地可达的块释放操作。
     *
     * @param manager 指向可用的 `VolumeManager` 实例；传入 `nullptr` 表示移除。
     */
    void set_volume_manager(std::shared_ptr<VolumeManager> manager);

    /**
     * @brief 注册/注销句柄观察者回调。
     *
     * MDS 使用弱引用持有观察者，避免强引用导致循环依赖。
     * 当需要强制关闭客户端句柄时，MDS 会调用 `notify_handle_observer`。
     *
     * @param observer 指向实现了 `IHandleObserver` 的观察者的弱引用；
     *                 传入空的 `weak_ptr` 表示注销当前观察者。
     */
    void set_handle_observer(std::weak_ptr<IHandleObserver> observer);
};