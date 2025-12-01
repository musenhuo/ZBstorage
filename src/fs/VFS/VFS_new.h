#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <boost/dynamic_bitset.hpp>
#include "../../mds/server/Server.h"
#include "../volume/VolumeRegistry.h"
#include "../volume/Volume.h"
#include "../../mds/inode/inode.h"
#include "../volume/VolumeManager.h"
#include "../handle/handle.h"

class FileSystemHandleObserver;

/**
 * @brief FileSystem 作为 VFS 的轻量入口，负责将文件/目录操作委托给 MdsServer，
 *        并将卷管理相关逻辑委托给 VolumeRegistry。
 */
class FileSystem {
private:
    friend class FileSystemHandleObserver;

    std::shared_ptr<MdsServer> mds_;                  ///< 元数据服务实例
    std::shared_ptr<VolumeManager> volume_manager_;   ///< 卷管理器（负责 IO 下发）
    std::unordered_map<int, FdTableEntry> fd_table_;  ///< 文件描述符表
    boost::dynamic_bitset<> fd_bitmap_;               ///< 文件描述符分配位图（1 表示空闲）
    mutable std::mutex fd_mutex_;                     ///< 保护 fd_table_ 的互斥量
    std::shared_ptr<FileSystemHandleObserver> handle_observer_;

    /**
     * @brief 在持锁状态下申请一个空闲 fd。
     * @return 成功返回 fd，失败返回 -1。
     */
    int acquire_fd_locked();
    /**
     * @brief 在持锁状态下释放 fd。
     * @param fd 待释放的文件描述符。
     */
    void release_fd_locked(int fd);

    /**
     * @brief 分配新的文件描述符并保存其条目。
     * @param inode 文件对应的 inode。
     * @param flags 打开标志。
     * @return 分配到的 fd，失败返回 -1。
     */
    int allocate_fd_locked(std::shared_ptr<Inode> inode, int flags);

    /**
     * @brief 在持锁状态下查找 fd 条目。
     * @param fd 目标文件描述符。
     * @return 条目指针，未找到返回 nullptr。
     */
    FdTableEntry* find_fd_locked(int fd);
    void force_close_handles(uint64_t inode);

public:
    /**
     * @brief 构造函数，内部会创建一个默认的 MdsServer 实例。
     * @param create_new 为 true 时初始化新的元数据存储；否则复用已有数据。
     */
    explicit FileSystem(bool create_new = true);
    ~FileSystem();

    /**
     * @brief 构造函数，可显式注入 MdsServer 与 VolumeRegistry。
     * @param mds 元数据服务实例，不能为空。
     * @param volume_registry 卷注册中心实例，可为 nullptr（表示暂不管理卷）。
     * @param volume_manager 卷管理器实例，可为 nullptr（表示暂不管理卷）。
     */
    FileSystem(std::shared_ptr<MdsServer> mds,
               std::shared_ptr<IVolumeRegistry> volume_registry,
               std::shared_ptr<VolumeManager> volume_manager = nullptr);

    /**
     * @brief 获取底层元数据服务对象。
     * @return MdsServer 智能指针。
     */
    std::shared_ptr<MdsServer> metadata() const noexcept;

    /**
     * @brief 获取卷注册中心。
     * @return VolumeRegistry 接口智能指针；若未注入则返回 nullptr。
     */
    std::shared_ptr<IVolumeRegistry> volume_registry() const noexcept;

    /**
     * @brief 获取卷管理器。
     * @return VolumeManager 智能指针；若未注入则返回 nullptr。
     */
    std::shared_ptr<VolumeManager> volume_manager() const noexcept;

    /**
     * @brief 设置卷管理器。
     * @param manager 卷管理器实例。
     */
    void set_volume_manager(std::shared_ptr<VolumeManager> manager);

    /**
     * @brief 创建根目录（若已存在则返回 true，用于幂等初始化）。
     * @return 操作是否成功。
     */
    bool create_root_directory();

    /**
     * @brief 创建普通文件。
     * @param path 绝对路径。
     * @param mode 文件权限位。
     * @return 操作是否成功。
     */
    bool create_file(const std::string& path, mode_t mode);

    /**
     * @brief 删除普通文件。
     * @param path 绝对路径。
     * @return 删除是否成功。
     */
    bool remove_file(const std::string& path);

    /**
     * @brief 创建目录。
     * @param path 绝对路径。
     * @param mode 目录权限位。
     * @return 操作是否成功。
     */
    bool mkdir(const std::string& path, mode_t mode);

    /**
     * @brief 删除目录（要求目录为空）。
     * @param path 绝对路径。
     * @return 删除是否成功。
     */
    bool rmdir(const std::string& path);

    /**
     * @brief 列出目录项并输出到标准输出。
     * @param path 绝对路径。
     * @return 操作是否成功。
     */
    bool ls(const std::string& path);

    /**
     * @brief 根据绝对路径查询 inode 号。
     * @param abs_path 绝对路径。
     * @return 对应的 inode 号，未找到返回 -1。
     */
    uint64_t lookup_inode(const std::string& abs_path) const;

    /**
     * @brief 根据绝对路径读取 inode 对象。
     * @param path 绝对路径。
     * @return inode 智能指针；未找到返回 nullptr。
     */
    std::shared_ptr<Inode> find_inode_by_path(const std::string& path) const;

    /**
     * @brief 获取根目录 inode 号。
     * @return 根目录 inode 号。
     */
    uint64_t get_root_inode() const;

    /**
     * @brief 收集最“冷”的若干个 inode。
     * @param max_candidates 最大返回数量。
     * @param min_age_windows 冷数据时间窗口阈值。
     * @return 符合条件的 inode 列表。
     */
    std::vector<uint64_t> collect_cold_inodes(size_t max_candidates,
                                              size_t min_age_windows);

    /**
     * @brief 生成冷数据 inode 的位图表示。
     * @param min_age_windows 冷数据时间窗口阈值。
     * @return 位图智能指针，按 inode 号下标标识冷数据。
     */
    std::shared_ptr<boost::dynamic_bitset<>> collect_cold_inodes_bitmap(size_t min_age_windows);

    /**
     * @brief 按访问时间百分位收集冷数据 inode。
     * @param percent 选取最老的百分比（0~100）。
     * @return 选出的 inode 号列表。
     */
    std::vector<uint64_t> collect_cold_inodes_by_atime_percent(double percent);

    /**
     * @brief 重建内存中的路径→inode 映射表。
     */
    void rebuild_inode_table();

    /**
     * @brief 注册卷信息并可选持久化。
     * @param vol 待注册的 Volume。
     * @param type 卷类型（SSD/HDD）。
     * @param out_index 可选输出参数，返回注册后的索引。
     * @param persist_now 是否立即持久化注册信息。
     * @return 注册是否成功。
     */
    bool register_volume(const std::shared_ptr<Volume>& vol,
                         VolumeType type,
                         int* out_index = nullptr,
                         bool persist_now = false);
    bool register_volume(const std::shared_ptr<Volume>& vol);

    /**
     * @brief 打开或创建文件并返回文件描述符。
     * @param path 绝对路径。
     * @param flags 打开标志。
     * @param mode 若需创建文件时使用的权限。
     * @return 成功返回 fd，失败返回 -1。
     */
    int open(const std::string& path, int flags, mode_t mode = 0644);

    /**
     * @brief 关闭文件描述符。
     * @param fd 待关闭的文件描述符。
     * @return 成功返回 0，失败返回 -1。
     */
    int close(int fd);
    int shutdown_fd(int fd);

    /**
     * @brief 调整文件当前读写偏移。
     * @param fd 文件描述符。
     * @param offset 偏移量。
     * @param whence 偏移基准（SEEK_SET/SEEK_CUR/SEEK_END）。
     * @return 调整后的偏移，失败返回 -1。
     */
    off_t seek(int fd, off_t offset, int whence);

    /**
     * @brief 写入文件。
     * @param fd 文件描述符。
     * @param buf 待写入数据缓冲区。
     * @param count 写入字节数。
     * @return 实际写入字节，失败返回 -1。
     */
    ssize_t write(int fd, const char* buf, size_t count);

    /**
     * @brief 读取文件。
     * @param fd 文件描述符。
     * @param buf 目标缓冲区。
     * @param count 读取字节数。
     * @return 实际读取字节，失败返回 -1。
     */
    ssize_t read(int fd, char* buf, size_t count);

    /**
     * @brief 启动文件系统，执行根目录创建与卷注册恢复。
     * @return 启动是否成功。
     */
    bool startup();

    /**
     * @brief 关闭文件系统，持久化卷注册信息等。
     * @return 关闭是否成功。
     */
    bool shutdown();
};