#pragma once
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <boost/dynamic_bitset.hpp>
#include "../handle/handle.h"
#include "../inode/inode.h"
#include "../inode/MetadataManager.h"
// #include "AccessTracker.h"

// 如未提供具体定义，进行前向声明以减少头部依赖
class AccessTracker;

// 卷类型枚举
enum class VolumeType {
    SSD,
    HDD
};

class FileSystem {
// 成员变量
public:
    std::vector<std::shared_ptr<Volume>> ssd_volumes; // 存储卷列表
    std::vector<std::shared_ptr<Volume>> hdd_volumes; // 存储卷列表
    // new 新增：记录每个内存卷在持久化文件中的位置（第几个，基于 meta 的前缀数组索引）
    std::vector<uint32_t> ssd_volume_indices;
    std::vector<uint32_t> hdd_volume_indices;
    // new 内存分配的“下一卷索引”（用于 persist_now=false 场景保证唯一）
    uint32_t ssd_next_index = 0;
    uint32_t hdd_next_index = 0;

    std::unordered_map<std::string, uint64_t> inode_table;
    std::unordered_map<int, FdTableEntry> fd_table; // 修改为使用FdTableEntry

    boost::dynamic_bitset<> fd_bitmap; // fd号分配位图

    // new 冷数据收集
    std::unique_ptr<AccessTracker> access_tracker; // 访问追踪器的抽象指针
    std::thread access_collector_thread; // 后台线程句柄
    std::atomic_bool access_collector_running{false}; // 后台线程的运行开关标志
    std::mutex access_collector_mtx; // 保护条件变量,用于与条件变量配合的互斥锁。
    std::condition_variable access_collector_cv; // 条件变量，用于在需要停止或提前轮换时唤醒后台线程
    std::chrono::milliseconds access_period{std::chrono::minutes(10)}; // 窗口轮换周期（例如 10 分钟）
    size_t access_window_count = 6; // 滑动窗口数量 M（最近 M 个周期）
    size_t access_bits_per_filter = 1024 * 1024 * 8; // 每个 Bloom 过滤器的位数（以“bit”为单位）
    size_t access_hash_count = 4; // Bloom 过滤器使用的哈希函数数量 k。
    // 游标：增量遍历已分配 inode（用于 collect 函数）
    uint64_t access_scan_cursor = 0; // 增量扫描游标（inode 槽号），记录 collect_cold_inodes 上次扫描位置。
    std::mutex access_scan_mtx; // 保护 access_scan_cursor 的互斥锁（并发访问时更新/读取游标需加锁）

// 成员函数
public:    
    void start_access_collector(std::chrono::milliseconds period = std::chrono::minutes(10),
                                size_t window_count = 6,
                                size_t bits_per_filter = 1024 * 1024 * 8, // 默认 1M bytes -> 8M bits
                                size_t hash_count = 4);
    void stop_access_collector();
    // 调用点：在 open/read 等路径中 mark 访问
    void mark_inode_accessed(uint64_t ino);
    // 手动触发一次收集，返回候选 inode 列表（最多 max_candidates） 旧接口
    std::vector<uint64_t> collect_cold_inodes(size_t max_candidates = 1024, size_t min_age_windows = 1);
    // 新接口（主用）：返回表示冷inode的位图指针（位 = 1 表示该 inode 被判定为冷）
    // 位图长度为 metadata_manager->get_total_inodes()
    std::shared_ptr<boost::dynamic_bitset<>> collect_cold_inodes_bitmap(size_t min_age_windows = 1);

    // 实现扫盘逻辑的接口
    // 扫描持久化存储上的所有已分配 inode，按最后访问时间排序，
    // 返回最老的前 percent% 的 inode 列表（percent: 0.0-100.0）
    std::vector<uint64_t> collect_cold_inodes_by_atime_percent(double percent);

    std::unique_ptr<MetadataManager> metadata_manager;

    FileSystem(bool create_new = true, int fd_bitmap_size = 4096);

    // 读取 meta 文件中的卷数量
    int get_persisted_ssd_volume_count(const std::string& meta_filename = SSD_VOLUME_META_PATH) const;
    int get_persisted_hdd_volume_count(const std::string& meta_filename = HDD_VOLUME_META_PATH) const;

    // 从 meta+data 读取第 index 个卷并追加到系统卷列表
    bool load_nth_ssd_volume(uint32_t index,
                             const std::string& meta_filename = SSD_VOLUME_META_PATH,
                             const std::string& data_filename = SSD_VOLUME_DATA_PATH);
    bool load_nth_hdd_volume(uint32_t index,
                             const std::string& meta_filename = HDD_VOLUME_META_PATH,
                             const std::string& data_filename = HDD_VOLUME_DATA_PATH);

    // 将内存中的卷持久化到第 index 个位置（index==count 表示追加到末尾）
    // 同尺寸替换将仅覆盖 data 文件，不重写全量；追加仅更新 data 尾部与 meta 尾部
    bool persist_ssd_volume_at(uint32_t index, const std::shared_ptr<Volume>& vol,
                               const std::string& meta_filename = SSD_VOLUME_META_PATH,
                               const std::string& data_filename = SSD_VOLUME_DATA_PATH);
    bool persist_hdd_volume_at(uint32_t index, const std::shared_ptr<Volume>& vol,
                               const std::string& meta_filename = HDD_VOLUME_META_PATH,
                               const std::string& data_filename = HDD_VOLUME_DATA_PATH);

    // 追加帮助函数（等价于 persist_*_volume_at(count, vol, ... )）
    bool append_ssd_volume(const std::shared_ptr<Volume>& vol,
                           const std::string& meta_filename = SSD_VOLUME_META_PATH,
                           const std::string& data_filename = SSD_VOLUME_DATA_PATH);
    bool append_hdd_volume(const std::shared_ptr<Volume>& vol,
                           const std::string& meta_filename = HDD_VOLUME_META_PATH,
                           const std::string& data_filename = HDD_VOLUME_DATA_PATH);

    // 可考虑作为私有方法
    // 通用：读取 meta 中 count（4字节，LE）
    int  get_volume_count_core(const std::string& meta_filename) const;

    // 通用：从 meta+data 读取第 index 个卷并追加到 out_list
    bool load_nth_volume_core(const std::string& meta_filename,
                               const std::string& data_filename,
                               uint32_t index,
                               std::vector<std::shared_ptr<Volume>>& out_list);

    // 通用：持久化卷到第 index 项（index==count => 追加）
    // 替换：若新旧尺寸相同 -> 仅覆盖 data；否则（不支持同文件内移动）需回退到外部重打包策略
    bool persist_volume_at_index_core(const std::string& meta_filename,
                                       const std::string& data_filename,
                                       uint32_t index,
                                       const Volume& vol);

    // 辅助：确保 meta 存在；当不存在时创建并写入 count=0
    static bool ensure_meta_initialized(const std::string& meta_filename);

    // 辅助：读取 meta 的两段前缀和（prefix[index-1], prefix[index]），以及 count
    static bool read_meta_prefix_pair(const std::string& meta_filename,
                                      uint32_t index,
                                      uint32_t& out_count,
                                      uint64_t& out_prev_prefix,
                                      uint64_t& out_cur_prefix);

    // 辅助：读取 meta 中第 last 项前缀和（用于追加）
    static bool read_meta_last_prefix(const std::string& meta_filename,
                                      uint32_t& out_count,
                                      uint64_t& out_last_prefix);

    // 辅助：在 meta 尾部追加一个前缀和，并把 count+1 写回文件头
    static bool append_meta_prefix(const std::string& meta_filename,
                                   uint64_t new_prefix,
                                   uint32_t new_count);

    bool persist_all_volumes(const std::vector<std::shared_ptr<Volume>>& volumes, const std::string& filename);

    std::vector<std::shared_ptr<Volume>> restore_all_volumes(const std::string& filename);

    bool persist_ssd_hdd_volumes(const std::string& ssd_filename = SSD_VOLUME_INFO_PATH, const std::string& hdd_filename = HDD_VOLUME_INFO_PATH);

    // 从文件中还原SSD卷和HDD卷
    bool restore_ssd_hdd_volumes(const std::string& ssd_filename = SSD_VOLUME_INFO_PATH, const std::string& hdd_filename = HDD_VOLUME_INFO_PATH);

    // 系统启动：加载所有持久化信息
    bool startup();

    // 系统关闭：持久化所有系统信息
    bool shutdown();

    // ！!仅用于测试
    // void rebuild_inode_table();
    void rebuild_inode_table();

    /**************************** demo ****************************/

    // 注册卷空间声明
    bool register_volume(
        const std::string& uuid,
        const std::string& storage_node_id,
        size_t total_blocks,
        VolumeType type,
        size_t block_size = 4096,
        size_t blocks_per_group = 64
    );

    // 直接注册已有卷
    // bool register_volume(const std::shared_ptr<Volume>& vol, VolumeType type, int* count);

    bool register_volume(const std::shared_ptr<Volume>& vol, VolumeType type);

    // 直接注册已有卷：创建后立即持久化并记录索引（out_index 可选返回其在持久化文件中的位置）
    bool register_volume2(const std::shared_ptr<Volume>& vol, VolumeType type, int* out_index = nullptr, bool persist_now = false);

    void alloc_volume_for_inode(const std::shared_ptr<Inode>& inode); // 为inode分配卷空间
    std::shared_ptr<Volume> find_volume_by_inode(const std::shared_ptr<Inode>& inode); // 根据inode信息查找对应卷

    // bool create_file(const std::string& path, uint16_t mode);
    bool create_file(const std::string& path, mode_t mode);
    bool remove_file(const std::string& path);
    ssize_t write(int fd, const char* buf, size_t count);
    ssize_t read(int fd, char* buf, size_t count);
    void close(int fd);

    //plus版
    int open(const std::string& path, int flags, mode_t mode = 0666);
    ssize_t write(int fd, const void* buf, size_t count);
    ssize_t read(int fd, void* buf, size_t count);
    // int close(int fd);

    //文件操作
    // bool create_file(const std::string& path, mode_t mode);
    bool unlink(const std::string& path);
    bool rename(const std::string& old_path, const std::string& new_path);
    bool mkdir(const std::string& path, mode_t mode);
    bool rmdir(const std::string& path);
    int symlink(const std::string& target, const std::string& linkpath);
    ssize_t readlink(const std::string& path, char* buf, size_t size);

    // 文件同步
    int fsync(int fd); //同步文件系统状态到磁盘
    int fdatasync(int fd); // 同步文件数据到磁盘————只刷数据，不刷元数据

    // 元数据操作
    int stat(const std::string& path, struct stat* buf);
    int fstat(int fd, struct stat* buf);

    // 目录操作
    ZBSS_DIR* opendir(const std::string& path);
    struct ZBSS_dirent* readdir(ZBSS_DIR* dirp);
    int closedir(ZBSS_DIR* dirp);

    // 文件锁
    int flock(int fd, int operation);
    int fcntl_lock(int fd, int cmd, struct flock* lock);

    // 权限管理
    int access(const std::string& path, int mode);
    int chmod(const std::string& path, mode_t mode);
    int fchmod(int fd, mode_t mode);

    // 分配新inode
    uint64_t allocate_inode(mode_t mode);
    
    // 创建硬链接
    bool link(const std::string& oldpath, const std::string& newpath);
    
    // 查找空闲文件描述符
    int get_free_fd();

    // @wbl
    // 返回根目录 inode 号
    uint64_t get_root_inode() const;

    // 工具函数
    bool addDirectoryEntry(const std::shared_ptr<Inode>& dir_inode,const DirectoryEntry& new_entry);
    bool removeDirectoryEntry(const std::shared_ptr<Inode>& dir_inode, const std::string& name);

    std::vector<DirectoryEntry> readDirectoryEntries(const std::shared_ptr<Inode>& dir_inode);

    // 根据路径递归查找目标文件 inode
    std::shared_ptr<Inode> find_inode_by_path(const std::string& path);

    // 根据绝对路径查找 inode 号
    uint64_t get_inode_number(const std::string& abs_path);

    // 创建根节点
    bool create_root_directory();   

    // 工具函数
    bool ls(const std::string& path);
};