#pragma once
#include <cstdint>
#include <string>

// 前向声明，避免环依赖
class Volume;

// ====================宏常量（保持不变） ====================
// 容量（字节）
#define SSD_DEFAULT_CAPACITY      (1ULL * 1024 * 1024 * 1024 * 1024)   // 1TB
#define HDD_DEFAULT_CAPACITY      (8ULL * 1024 * 1024 * 1024 * 1024)   // 8TB
#define OPTICAL_DISC_CAPACITY     (100ULL * 1024 * 1024 * 1024)        // 100GB

// SSD寿命参数
#define SSD_DEFAULT_TBW           (600ULL * 1024 * 1024 * 1024 * 1024) // 600TBW
#define SSD_DEFAULT_ERASE_CYCLES  2000                                  // 每块2000次擦写

// HDD寿命参数
#define HDD_DEFAULT_MTBF_HOURS    1000000                                // 100万小时
#define HDD_EXPECTED_YEARS        5                                      // 5年

// 块大小（字节）
#define SSD_BLOCK_SIZE            (128 * 1024 * 1024)           // 128MB
#define HDD_BLOCK_SIZE            (512 * 1024 * 1024)    // 512MB

// 每个块组的块数
#define SSD_BLOCKS_PER_GROUP      1024
#define HDD_BLOCKS_PER_GROUP      512

// 吞吐率（MB/s）
#define SSD_DEFAULT_READ_MBPS     500.0
#define SSD_DEFAULT_WRITE_MBPS    450.0
#define HDD_DEFAULT_READ_MBPS     200.0
#define HDD_DEFAULT_WRITE_MBPS    180.0
#define OPTICAL_DISC_READ_MBPS    36.0
#define OPTICAL_DISC_WRITE_MBPS   36.0

// 光盘库参数
#define OPTICAL_LIBRARY_DISC_NUM     20000    // 每个光盘库光盘数量
#define OPTICAL_LIBRARY_DRIVE_COUNT  10
#define OPTICAL_LIBRARY_LOAD_TIME    10.0     // 秒

// ==================== 原枚举（保持不变） ====================
enum class DiscStatus {
    Blank,      // 空盘
    InUse,      // 正在使用
    Recycled,   // 已回收
    Finalized,  // 已刻录完成，只读
    Lost        // 丢失
};

enum class StorageNodeType {
    SSD,
    HDD,
    Mix, // 混合存储节点，包含SSD和HDD
};
