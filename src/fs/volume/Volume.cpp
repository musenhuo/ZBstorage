#include "Volume.h"
#include "../io/IIOGateway.h"
#include "srm/storage_manager/StorageResource.h"


Volume::Volume(const std::string& uuid,
               const std::string& storage_node_id,
               std::chrono::system_clock::time_point creation_time,
               std::unique_ptr<BlockManager> block_manager,
               bool readonly)
    : uuid_(uuid),
      storage_node_id_(storage_node_id),
      creation_time_(creation_time),
      block_manager_(std::move(block_manager)),
      readonly_(readonly)
{}

void Volume::resize(size_t new_total_blocks) {
    // 实现动态调整卷大小的逻辑
    // 需要创建新的BlockManager并迁移数据
    // (此处为简化为仅更新元数据)
    auto new_manager = std::make_unique<BlockManager>(
        new_total_blocks, 
        block_manager_->block_size(),
        block_manager_->blocks_per_group()
    );
    
    // 实际实现需要迁移已有块的分配状态
    // 这里仅做示意性替换
    block_manager_ = std::move(new_manager);
}

void Volume::safe_free_blocks(const BlockSegment& seg) {
    if (readonly_) 
        throw std::runtime_error("卷处于只读模式");
    
    // 验证段范围有效性
    if (seg.start_block + seg.block_count > total_blocks()) 
        throw std::out_of_range("块段超出卷范围");
    
    // 检查所有块是否都已分配
    for (size_t i = 0; i < seg.block_count; i++) {
        size_t block_id = seg.start_block + i;
        try {
            // 尝试访问分配信息
            [[maybe_unused]] auto info = block_manager_->get_block_info(block_id);
        } catch (const std::out_of_range&) {
            throw std::logic_error("尝试释放未分配的块: " + std::to_string(block_id));
        }
    }
    
    // 执行实际释放
    block_manager_->free_blocks(seg);
}


void Volume::submit_io_request(const IORequest& req) {
    if (io_gateway_) {
        double process_time = io_gateway_->processIO(req);
        if (process_time < 0) {
            std::cerr << "[Volume] IO处理失败: " << req.start_block 
                      << ", 块数: " << req.block_count << std::endl;
        } else {
            std::cout << "[Volume] IO处理成功, 耗时: " << process_time << " 秒" << std::endl;
        }
        return;
    }

    if (!g_storage_resource) {
        std::cerr << "[Volume] 错误: 全局存储资源未初始化" << std::endl;
        return;
    }
    
    // 创建包含卷信息的IORequest
    IORequest volume_req = req;
    volume_req.storage_node_id = storage_node_id_;
    volume_req.volume_id = uuid_;
    
    // 调用存储资源处理IO - 使用请求内置的buffer
    double process_time = g_storage_resource->processIO(volume_req);
    
    if (process_time < 0) {
        std::cerr << "[Volume] IO处理失败: " << req.start_block 
                  << ", 块数: " << req.block_count << std::endl;
    } else {
        std::cout << "[Volume] IO处理成功, 耗时: " << process_time << " 秒" << std::endl;
    }
}


void Volume::submit_io_requests(const std::vector<IORequest>& reqs) {
    if (reqs.empty()) return;

    if (io_gateway_) {
        io_gateway_->processIOBatch(reqs);
        return;
    }

    // 合并相邻请求
    auto merged = merge_adjacent_requests(reqs);
    
    // 批量下发
    for (const auto& req : merged) {
        submit_io_request(req);
    }
}


std::vector<IORequest> Volume::merge_adjacent_requests(
    const std::vector<IORequest>& reqs) {
    
    if (reqs.empty()) return {};
    
    std::vector<IORequest> merged;
    
    // 先按块号排序
    std::vector<size_t> indices(reqs.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return reqs[a].start_block < reqs[b].start_block;
    });
    
    IORequest current_req = reqs[indices[0]];
    
    for (size_t i = 1; i < indices.size(); ++i) {
        const auto& next_req = reqs[indices[i]];
        
        // 检查是否可以合并（相邻块且相同类型）
        // 并且检查缓冲区是否连续
        bool buffers_continuous = false;
        if (current_req.buffer && next_req.buffer) {
            char* current_end = static_cast<char*>(current_req.buffer) + current_req.data_size;
            buffers_continuous = (current_end == static_cast<char*>(next_req.buffer));
        }
        
        if (current_req.type == next_req.type &&
            current_req.start_block + current_req.block_count == next_req.start_block &&
            current_req.offset_in_block == 0 && next_req.offset_in_block == 0 &&
            buffers_continuous) {
            
            // 合并
            current_req.block_count += next_req.block_count;
            current_req.data_size += next_req.data_size;
            current_req.buffer_size = current_req.data_size; // 更新缓冲区大小
            
        } else {
            // 不能合并，保存当前请求
            merged.push_back(current_req);
            current_req = next_req;
        }
    }
    
    // 添加最后一个请求
    merged.push_back(current_req);
    
    return merged;
}

std::vector<uint8_t> Volume::serialize() const {
    std::vector<uint8_t> buf;
    
    // 辅助函数：追加数据到缓冲区
    auto append = [&](const void* data, size_t size) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), ptr, ptr + size);
    };
    
    // 1. 序列化 UUID
    uint16_t uuid_len = uuid_.size();
    append(&uuid_len, sizeof(uuid_len));
    append(uuid_.data(), uuid_len);
    
    // 2. 序列化存储节点 ID
    uint16_t node_id_len = storage_node_id_.size();
    append(&node_id_len, sizeof(node_id_len));
    append(storage_node_id_.data(), node_id_len);
    
    // 3. 序列化创建时间
    auto time_since_epoch = creation_time_.time_since_epoch();
    auto time_value = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch).count();
    append(&time_value, sizeof(time_value));
    
    // 4. 序列化只读标志
    append(&readonly_, sizeof(readonly_));
    
    // 5. 序列化 BlockManager
    auto block_manager_data = block_manager_->serialize();
    uint32_t bm_size = block_manager_data.size();
    append(&bm_size, sizeof(bm_size));
    append(block_manager_data.data(), bm_size);
    
    return buf;
}

std::unique_ptr<Volume> Volume::deserialize(const uint8_t* data, size_t size) {
    size_t offset = 0;
    
    // 安全读取函数
    auto safe_read = [&](void* dest, size_t len) -> bool {
        if (offset + len > size) return false;
        std::memcpy(dest, data + offset, len);
        offset += len;
        return true;
    };
    
    // 1. 反序列化 UUID
    uint16_t uuid_len;
    if (!safe_read(&uuid_len, sizeof(uuid_len))) return nullptr;
    if (offset + uuid_len > size) return nullptr;
    std::string uuid(reinterpret_cast<const char*>(data + offset), uuid_len);
    offset += uuid_len;
    
    // 2. 反序列化存储节点 ID
    uint16_t node_id_len;
    if (!safe_read(&node_id_len, sizeof(node_id_len))) return nullptr;
    if (offset + node_id_len > size) return nullptr;
    std::string storage_node_id(reinterpret_cast<const char*>(data + offset), node_id_len);
    offset += node_id_len;
    
    // 3. 反序列化创建时间
    int64_t time_value;
    if (!safe_read(&time_value, sizeof(time_value))) return nullptr;
    auto creation_time = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(time_value));
    
    // 4. 反序列化只读标志
    bool readonly;
    if (!safe_read(&readonly, sizeof(readonly))) return nullptr;
    
    // 5. 反序列化 BlockManager
    uint32_t bm_size;
    if (!safe_read(&bm_size, sizeof(bm_size))) return nullptr;
    
    auto block_manager = BlockManager::deserialize(data, offset, size);
    if (!block_manager) return nullptr;
    
    // 6. 创建 Volume 实例
    // 注意：这里需要一个特殊的构造函数
    auto volume = std::unique_ptr<Volume>(new Volume(
        uuid, storage_node_id, creation_time, std::move(block_manager), readonly));
    
    return volume;
}

// 持有 IIOGateway* 或智能指针，并提供 setter（此处示意）
// 在 submit_io_request(s) 内通过网关转发，而不是直接用 g_storage_resource