#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include "inode.h"

// --- InodeStorage: 管理 inode 的存储 ---
class InodeStorage {
// 成员变量
private:
    std::fstream inode_file;
    std::string file_path;
    mutable std::mutex file_mutex;
public:
    // 为每个inode在磁盘上分配固定大小，支持通过ino进行随机访问。
    // 序列化后的inode都会被填充到这个大小
    static constexpr size_t INODE_DISK_SLOT_SIZE = 512; // 目前规定每个inode大小512B

    // 批量生成参数结构
    struct NodeDistributionEntry {
        uint16_t node_id = 0;
        uint8_t node_type = 0; // 0 SSD,1 HDD,2 Mix,3 Reserved
        double weight = 1.0;
    };

    struct TemperatureRatio {
        double hot = 0.15;
        double warm = 0.35;
        double cold = 0.50;
    };

    struct SizeRange {
        uint64_t min_bytes = 0;
        uint64_t max_bytes = 0;
    };

    struct BatchGenerationConfig {
        std::string output_file;               // 生成的批量文件路径
        size_t batch_size = 1'000'000;         // 单文件包含的 inode 数
        uint64_t starting_inode = 0;           // 起始 inode 号
        TemperatureRatio temp_ratio;           // 冷/温/热比例
        SizeRange hot_range{64ULL << 20, 512ULL << 20};   // 64MB ~ 512MB
        SizeRange warm_range{8ULL << 20, 64ULL << 20};    // 8MB ~ 64MB
        SizeRange cold_range{1ULL << 20, 8ULL << 20};     // 1MB ~ 8MB
        std::vector<NodeDistributionEntry> node_distribution; // 节点/类型权重
        size_t max_segments = 4;               // 一个 inode 最多拆成多少段
        uint64_t block_size_bytes = 4ULL * 1024 * 1024;    // 块大小，用于估算 block_segments
        uint32_t random_seed = 0;              // 0 表示使用随机种子
        bool verbose = true;                   // 是否打印进度
        std::string root_path = "/dataset";   // 目录树根路径
        size_t dir_depth = 3;                  // 目录深度
        size_t dir_fanout = 16;                // 每层目录分支数
    };

// 成员函数
public:
    InodeStorage(const std::string& path, bool create_new = false);
    ~InodeStorage();
    // 写入指定编号的 inode
    bool write_inode(uint64_t ino, const Inode& dinode);
    // 读取指定编号的 inode
    bool read_inode(uint64_t ino, Inode& dinode);
    // 扩展 inode 文件到指定大小
    void expand(size_t new_size);
    // 获取 inode 文件大小
    size_t size();

    // 仅负责批量生成，不落盘到 inode_file
    static bool generate_metadata_batch(const BatchGenerationConfig& config);
};

// --- BitmapStorage: 管理 inode 分配位图的存储 ---
class BitmapStorage {
// 成员变量
private:
    std::fstream bitmap_file;
    std::string file_path;
    mutable std::mutex file_mutex;

// 成员函数
public:
    BitmapStorage(const std::string& path, bool create_new = false);
    ~BitmapStorage();
    // 写入位图数据 ！!重写整个位图文件
    // bitmap序列化
    bool write_bitmap(const std::vector<char>& bitmap_data);
    bool write_bitmap_region(size_t byte_offset, const char* data, size_t length);
    // 读取位图数据
    // bitmap反序列化
    void read_bitmap(std::vector<char>& bitmap_data);
};