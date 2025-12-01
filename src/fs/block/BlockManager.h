#pragma once
#include <vector>
#include <bitset>
#include <stdexcept>
#include <list>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <string>
// 系统存储配置参数
constexpr size_t BLOCK_SIZE = 1024 * 1024;                       // 每个块的字节大小（4KB）
constexpr size_t TOTAL_BLOCKS = 1024;                            // 系统管理的总块数（示例值）
constexpr size_t BLOCKS_PER_GROUP = 64;                          // 每个块组包含的块数
constexpr size_t GROUPS_COUNT = TOTAL_BLOCKS / BLOCKS_PER_GROUP; // 计算块组总数

struct BlockSegment
{
    size_t logical_start; // 文件内逻辑块起始号
    size_t start_block;   // 起始块号
    size_t block_count;   // 连续块的数量

    BlockSegment(size_t lstart = 0, size_t pstart = 0, size_t count = 0)
        : logical_start(lstart), start_block(pstart), block_count(count) {}

    std::string to_string() const
    {
        return "[" + std::to_string(logical_start) + "|" +
               std::to_string(start_block) + ":" +
               std::to_string(block_count) + "]";
    }
};

enum class AllocType
{
    DATA,   // 常规数据存储块
    INODE,  // 文件元数据存储块
    META,   // 系统元数据块（如超级块）
    JOURNAL // 日志系统专用块
};
struct BlockAllocInfo
{
    AllocType type;     ///< 块类型
    bool is_contiguous; ///< 是否属于连续分配区域
    size_t length = 1;  ///< 连续块长度
};

class BlockManager
{
private:
    struct BlockGroup
    {
        std::vector<bool> free_blocks; ///< 动态位图
        size_t free_count;             ///< 空闲块计数器
        size_t first_block;            ///< 本组起始全局块号
        size_t num_blocks;             ///< 本组实际块数
    };
    const size_t total_blocks_;                           // 卷内总块数
    const size_t block_size_;                             // 卷内块大小（字节）
    const size_t blocks_per_group_;                       // 卷内每组块数
    std::vector<BlockGroup> groups_;                      // 块组容器
    std::unordered_map<size_t, BlockAllocInfo> alloc_map; // 分配元数据
    std::list<size_t> free_groups;                        // 空闲组索引列表
    void initialize_structures();
    BlockSegment find_contiguous_segment(BlockGroup &group, size_t min_count) const;
    void mark_allocated_batch(const BlockSegment &seg);
    void record_allocation(const BlockSegment &seg, AllocType type, bool contiguous);
    BlockGroup &get_group(size_t block);

public:
    // 构造函数接收配置参数
    BlockManager(size_t total_blocks,
                 size_t block_size = 4096,
                 size_t blocks_per_group = 64)
        : total_blocks_(total_blocks),
          block_size_(block_size),
          blocks_per_group_(blocks_per_group)
    {
        initialize_structures();
    }

    /**
     * @brief 合并相邻块段
     * @param segments 待合并的段列表
     * @return 合并后的块段列表
     */
    std::vector<BlockSegment> merge_contiguous_segments(
        const std::vector<BlockSegment> &segments) const;

    BlockSegment allocate_block(AllocType type);

    /**
     * @brief 核心分配方法
     * @param type 分配类型
     * @param count 请求块数
     * @return 分配的块段列表
     */
    std::vector<BlockSegment> allocate_blocks(AllocType type, size_t count);

    /**
     * @brief 释放块段资源
     * @param seg 待释放的块段
     */
    void free_blocks(const BlockSegment &seg);

    /**
     * @brief 释放指定范围的块
     * @param block_start 起始块号
     * @param length 块数量
     */
    void free_blocks(size_t block_start, size_t length);

    /**
     * @brief 回滚分配（释放段）
     * @param seg 待回滚的块段
     */
    void release_blocks(const BlockSegment &seg);

    const BlockAllocInfo &get_block_info(size_t block) const;

    // 访问器方法
    size_t block_size() const { return block_size_; }
    size_t total_blocks() const { return total_blocks_; }
    size_t blocks_per_group() const { return blocks_per_group_; }
    size_t groups_count() const { return groups_.size(); }
    size_t free_blocks_count() const;
    // （反）序列化BlockManager信息
    std::vector<uint8_t> serialize() const;
    static std::unique_ptr<BlockManager> deserialize(const uint8_t *data, size_t &offset, size_t total_size);
    void rebuild_auxiliary_structures();
    const std::vector<BlockGroup> &groups() const { return groups_; }
};
