#pragma once
#include "../block/BlockManager.h"
#include "msg/IO.h"
#include "../io/IIOGateway.h"
#include <string>
#include <memory>
#include <chrono>
#include <numeric>
#include <iostream>

class StorageResource; 

// 声明外部全局变量
extern StorageResource* g_storage_resource;

class Volume {
private:
    const std::string uuid_;                             // 卷唯一标识
    std::string storage_node_id_;                       // 卷所在存储节点的id
    const std::chrono::system_clock::time_point creation_time_; // 创建时间
    std::unique_ptr<BlockManager> block_manager_;        // 专属块管理器
    bool readonly_ = false;                              // 只读标志
    std::shared_ptr<IIOGateway> io_gateway_; // 新增：IO 网关（可为空）

public:
    Volume(const std::string& uuid, 
            const std::string& storage_node_id,
           size_t total_blocks, 
           size_t block_size = 4096,
           size_t blocks_per_group = 64)
        : uuid_(uuid),
          storage_node_id_(storage_node_id),
          creation_time_(std::chrono::system_clock::now()),
          block_manager_(std::make_unique<BlockManager>(
              total_blocks, block_size, blocks_per_group))
    {}

    Volume(const std::string& uuid,
           const std::string& storage_node_id,
           std::chrono::system_clock::time_point creation_time,
           std::unique_ptr<BlockManager> block_manager,
           bool readonly);
    
    // 禁止拷贝
    Volume(const Volume&) = delete;
    Volume& operator=(const Volume&) = delete;
    
    // 块管理器访问器
    BlockManager& block_manager() { return *block_manager_; }
    const BlockManager& block_manager() const { return *block_manager_; }
    
    // 卷元数据访问器
    const std::string& uuid() const { return uuid_; }
    const std::string& storage_node_id() const { return storage_node_id_; }
    size_t total_blocks() const { return block_manager_->total_blocks(); }
    size_t used_blocks() const { 
        return total_blocks() - block_manager_->free_blocks_count(); 
    }
    double usage_percentage() const {
        return static_cast<double>(used_blocks()) / total_blocks() * 100.0;
    }
    std::chrono::system_clock::time_point creation_time() const { 
        return creation_time_; 
    }

    /**
     * @brief 分配单个存储块
     * @param type 块类型
     * @return 分配的块段
     * @throw std::runtime_error 当卷为只读或空间不足时
     */
    BlockSegment allocate_block(AllocType type) {
        if (readonly_) throw std::runtime_error("卷处于只读模式");
        return block_manager_->allocate_block(type);
    }
    
    /**
     * @brief 分配多个存储块
     * @param type 块类型
     * @param count 请求的块数量
     * @return 分配的块段列表
     * @throw std::runtime_error 当卷为只读或空间不足时
     */
    std::vector<BlockSegment> allocate_blocks(AllocType type, size_t count) {
        if (readonly_) throw std::runtime_error("卷处于只读模式");
        return block_manager_->allocate_blocks(type, count);
    }
    
    /**
     * @brief 释放块段
     * @param seg 要释放的块段
     * @throw std::runtime_error 当卷为只读时
     */
    void free_blocks(const BlockSegment& seg) {
        if (readonly_) throw std::runtime_error("卷处于只读模式");
        block_manager_->free_blocks(seg);
    }
    
    /**
     * @brief 释放指定范围的块
     * @param start_block 起始块号
     * @param count 块数量
     * @throw std::runtime_error 当卷为只读时
     */
    void free_blocks(size_t start_block, size_t count) {
        if (readonly_) throw std::runtime_error("卷处于只读模式");
        block_manager_->free_blocks(start_block, count);
    }
    
    /**
     * @brief 回滚分配（错误处理时使用）
     * @param seg 要回滚的块段
     */
    void release_blocks(const BlockSegment& seg) {
        // 即使只读模式下也允许回滚
        block_manager_->release_blocks(seg);
    }

    /**
     * @brief 获取块的分配信息
     * @param block 块号
     * @return 块的分配元数据
     * @throw std::out_of_range 当块号无效时
     */
    const BlockAllocInfo& get_block_info(size_t block) const {
        return block_manager_->get_block_info(block);
    }
    
    /**
     * @brief 获取连续块段的信息
     * @param start_block 连续区域的起始块
     * @return 包含长度和分配类型的信息
     * 
     * 此方法在访问连续存储区域时特别有用，
     * 可以一次性获取整个区域的元数据
     */
    BlockAllocInfo get_contiguous_segment_info(size_t start_block) const {
        const auto& info = get_block_info(start_block);
        if (info.is_contiguous) {
            return BlockAllocInfo{
                info.type,
                true,
                info.length
            };
        }
        // 如果是离散分配的块，则长度始终为1
        return BlockAllocInfo{info.type, false, 1};
    }
    
    /**
     * @brief 安全释放方法（带完整性检查）
     * @param seg 要释放的块段
     * 
     * 在执行释放前会验证：
     * 1. 块段中的所有块确实已分配
     * 2. 块段位于有效范围内
     */
    void safe_free_blocks(const BlockSegment& seg);

    // 卷管理方法
    void resize(size_t new_total_blocks);
    bool is_readonly() const { return readonly_; }
    void set_readonly(bool ro) { readonly_ = ro; }

    void write_block(size_t block_id, const void* data, size_t size, size_t offset = 0) {
        if (readonly_) throw std::runtime_error("卷处于只读模式");
        
        // 创建并提交IO请求
        IORequest req(IOType::Write, storage_node_id_, uuid_, 
                     block_id, 1, offset, size, 
                     const_cast<void*>(data), size);
        submit_io_request(req);
    }

    void read_block(size_t block_id, void* data, size_t size, size_t offset = 0) const {
        if (block_id >= total_blocks()) {
            throw std::out_of_range("Block ID out of range");
        }
        if (size > block_manager_->block_size()) {
            throw std::runtime_error("Data size exceeds block size");
        }
        
        // 创建并提交IO请求
        IORequest req(IOType::Read, storage_node_id_, uuid_, 
                     block_id, 1, offset, size, 
                     data, size);
        // 使用const_cast因为submit_io_request不是const方法
        const_cast<Volume*>(this)->submit_io_request(req);
    }

    // 单个IO请求 - 移除data参数
    void submit_io_request(const IORequest& req);
    
    // 批量IO请求 - 移除datas参数
    void submit_io_requests(const std::vector<IORequest>& reqs);
    
    // 合并相邻的IO请求 - 返回类型和参数都改变
    std::vector<IORequest> merge_adjacent_requests(
        const std::vector<IORequest>& reqs);

    // Volume.h
    std::vector<uint8_t> serialize() const;
    static std::unique_ptr<Volume> deserialize(const uint8_t* data, size_t size);
    
    void set_io_gateway(std::shared_ptr<IIOGateway> gw) { io_gateway_ = std::move(gw); }
    std::shared_ptr<IIOGateway> io_gateway() const { return io_gateway_; }
};
