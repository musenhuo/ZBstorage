#include "BlockManager.h"
#include "bits/stdc++.h"
#include "ZBLog.h"

void BlockManager::initialize_structures() {
    const size_t group_count = (total_blocks_ + blocks_per_group_ - 1) / blocks_per_group_;
    groups_.resize(group_count);
    
    for (size_t i = 0; i < group_count; ++i) {
        const size_t first_block = i * blocks_per_group_;
        const size_t blocks_in_group = std::min(
            blocks_per_group_, 
            total_blocks_ - first_block
        );
        
        groups_[i] = {
            std::vector<bool>(blocks_in_group, true), // 初始全部空闲
            blocks_in_group,                         // 空闲计数
            first_block,                             // 起始块号
            blocks_in_group                          // 本组实际块数
        };
        free_groups.push_back(i);
    }
}

std::vector<BlockSegment> BlockManager::merge_contiguous_segments(
    const std::vector<BlockSegment>& segments) const 
{
    if (segments.empty()) return {};
    
    std::vector<BlockSegment> sorted = segments;
    std::sort(sorted.begin(), sorted.end(), 
        [](const auto& a, const auto& b) {
            return a.start_block < b.start_block;
        });
    
    std::vector<BlockSegment> result;
    BlockSegment current = sorted[0];
    
    for (size_t i = 1; i < sorted.size(); ++i) {
        const auto& next = sorted[i];
        if (current.start_block + current.block_count == next.start_block) {
            current.block_count += next.block_count;
        } else {
            result.push_back(current);
            current = next;
        }
    }
    result.push_back(current);
    
    return result;
}

BlockSegment BlockManager::allocate_block(AllocType type) {
    auto segments = allocate_blocks(type, 1);
    return segments.front();
}

std::vector<BlockSegment> BlockManager::allocate_blocks(AllocType type, size_t count) {
    std::vector<BlockSegment> segments;
    size_t remaining = count;
    
    // 阶段1: 尝试连续分配
    auto freeGroupsCopy = free_groups;
    for (auto group_id : freeGroupsCopy) {
        auto& group = groups_[group_id];
        if (group.free_count < remaining) continue;
        // std::cout << 1 << std::endl;
        
        auto seg = find_contiguous_segment(group, remaining);
        LOGD("[ALLOC] 找到连续段: " << seg.to_string());
        // std::cout << "找到连续段: " << seg.to_string() << std::endl;

        if (seg.block_count >= remaining) {
            seg.block_count = remaining;
            mark_allocated_batch(seg);
            
            // 更新组状态
            if (group.free_count < group.num_blocks / 2) {
                free_groups.remove(group_id);
            }
            
            segments.push_back(seg);
            record_allocation(seg, type, true);
            return segments;
        }
    }
    
    // 阶段2: 离散分配
    while (remaining > 0) {
        bool progress = false;
        freeGroupsCopy = free_groups;
        
        for (auto group_id : freeGroupsCopy) {
            auto& group = groups_[group_id];
            if (group.free_count == 0) continue;
            
            auto seg = find_contiguous_segment(group, 1);
            if (seg.block_count > 0) {
                size_t alloc_count = std::min(remaining, seg.block_count);
                seg.block_count = alloc_count;
                
                mark_allocated_batch(seg);
                record_allocation(seg, type, false);
                segments.push_back(seg);
                
                remaining -= alloc_count;
                progress = true;
                
                // 更新组状态
                if (group.free_count < group.num_blocks / 2) {
                    free_groups.remove(group_id);
                }
                
                if (remaining == 0) break;
            }
        }
        
        if (!progress) {
            for (auto& seg : segments) {
                release_blocks(seg);
            }
            throw std::runtime_error("空间不足: ENOSPC");
        }
    }
    
    return merge_contiguous_segments(segments);
}

void BlockManager::free_blocks(const BlockSegment& seg) {
    free_blocks(seg.start_block, seg.block_count);
}

void BlockManager::free_blocks(size_t block_start, size_t length) {
    for (size_t i = 0; i < length; ) {
        auto& group = get_group(block_start + i);
        size_t group_offset = (block_start + i) - group.first_block;
        size_t max_in_group = std::min(length - i, group.num_blocks - group_offset);
        
        for (size_t j = 0; j < max_in_group; j++) {
            size_t block_id = block_start + i + j;
            size_t local_idx = group_offset + j;
            
            if (!group.free_blocks[local_idx]) {
                group.free_blocks[local_idx] = true;
                group.free_count++;
                alloc_map.erase(block_id);
            }
        }
        
        size_t group_idx = (block_start + i) / blocks_per_group_;
        if (group.free_count == group.num_blocks) {
            if (std::find(free_groups.begin(), free_groups.end(), group_idx) == free_groups.end()) {
                free_groups.push_back(group_idx);
            }
        }
        else if (group.free_count >= group.num_blocks / 2) {
            if (std::find(free_groups.begin(), free_groups.end(), group_idx) == free_groups.end()) {
                free_groups.push_back(group_idx);
            }
        }
        
        i += max_in_group;
    }
}

void BlockManager::release_blocks(const BlockSegment& seg) {
    free_blocks(seg.start_block, seg.block_count);
}

const BlockAllocInfo& BlockManager::get_block_info(size_t block) const {
    return alloc_map.at(block);
}

size_t BlockManager::free_blocks_count() const {
    size_t count = 0;
    for (const auto& group : groups_) {
        count += group.free_count;
    }
    return count;
}

BlockManager::BlockGroup& BlockManager::get_group(size_t block) {
    return groups_[block / blocks_per_group_];
}

// 找寻块组中最长的连续空闲段
BlockSegment BlockManager::find_contiguous_segment(
    BlockGroup& group, 
    size_t min_count
) const {
    size_t max_start = 0;
    size_t max_length = 0;
    size_t current_start = 0;
    size_t current_length = 0;
    
    for (size_t i = 0; i < group.num_blocks; ++i) {
        if (group.free_blocks[i]) {
            if (current_length == 0) 
                current_start = i;
            
            current_length++;
            
            if (current_length > max_length) {
                max_length = current_length;
                max_start = current_start;
            }
        } else {
            current_length = 0;
        }
    }
    
    if (max_length >= min_count) {
        return BlockSegment(0, group.first_block + max_start, max_length);
    }
    
    // return BlockSegment(0, max_length);
    return BlockSegment(0, 0, max_length); // 无可用段
}

void BlockManager::mark_allocated_batch(const BlockSegment& seg) {
    auto& group = get_group(seg.start_block);
    size_t group_offset = seg.start_block - group.first_block;
    
    for (size_t i = 0; i < seg.block_count; i++) {
        group.free_blocks[group_offset + i] = false;
    }
    
    group.free_count -= seg.block_count;
}

void BlockManager::record_allocation(
    const BlockSegment& seg,
    AllocType type,
    bool contiguous
) {
    for (size_t i = 0; i < seg.block_count; ++i) {
        size_t block_id = seg.start_block + i;
        alloc_map[block_id] = {
            type,
            contiguous,
            (i == 0) ? seg.block_count : 1
        };
    }
}

std::vector<uint8_t> BlockManager::serialize() const {
    std::vector<uint8_t> buf;
    
    // 辅助函数：追加数据到缓冲区
    auto append = [&](const void* data, size_t size) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), ptr, ptr + size);
    };
    
    // 1. 序列化基本配置参数
    append(&total_blocks_, sizeof(total_blocks_));
    append(&block_size_, sizeof(block_size_));
    append(&blocks_per_group_, sizeof(blocks_per_group_));
    
    // 2. 序列化块组数量
    uint32_t group_count = groups_.size();
    append(&group_count, sizeof(group_count));
    
    // 3. 序列化每个块组的核心信息
    for (const auto& group : groups_) {
        append(&group.free_count, sizeof(group.free_count));
        append(&group.first_block, sizeof(group.first_block));
        append(&group.num_blocks, sizeof(group.num_blocks));
        
        // 序列化位图 (std::vector<bool> 需要特殊处理)
        uint32_t bitmap_size = group.free_blocks.size();
        append(&bitmap_size, sizeof(bitmap_size));
        
        // 将 std::vector<bool> 转换为字节数组
        std::vector<uint8_t> bitmap_bytes((bitmap_size + 7) / 8, 0);
        for (size_t i = 0; i < bitmap_size; ++i) {
            if (group.free_blocks[i]) {
                bitmap_bytes[i / 8] |= (1 << (i % 8));
            }
        }
        append(bitmap_bytes.data(), bitmap_bytes.size());
    }
    
    // 注意：不序列化 alloc_map 和 free_groups，这些将在反序列化时重建
    
    return buf;
}

std::unique_ptr<BlockManager> BlockManager::deserialize(const uint8_t* data, size_t& offset, size_t total_size) {
    // 安全读取函数
    auto safe_read = [&](void* dest, size_t size) -> bool {
        if (offset + size > total_size) return false;
        std::memcpy(dest, data + offset, size);
        offset += size;
        return true;
    };
    
    // 1. 反序列化基本配置参数
    size_t total_blocks, block_size, blocks_per_group;
    if (!safe_read(&total_blocks, sizeof(total_blocks))) return nullptr;
    if (!safe_read(&block_size, sizeof(block_size))) return nullptr;
    if (!safe_read(&blocks_per_group, sizeof(blocks_per_group))) return nullptr;
    
    // 2. 创建 BlockManager 实例（不调用 initialize_structures）
    auto mgr = std::unique_ptr<BlockManager>(new BlockManager(total_blocks, block_size, blocks_per_group));
    mgr->groups_.clear(); // 清空构造时创建的默认组
    
    // 3. 反序列化块组数量
    uint32_t group_count;
    if (!safe_read(&group_count, sizeof(group_count))) return nullptr;
    
    mgr->groups_.resize(group_count);
    
    // 4. 反序列化每个块组
    for (uint32_t i = 0; i < group_count; ++i) {
        auto& group = mgr->groups_[i];
        
        if (!safe_read(&group.free_count, sizeof(group.free_count))) return nullptr;
        if (!safe_read(&group.first_block, sizeof(group.first_block))) return nullptr;
        if (!safe_read(&group.num_blocks, sizeof(group.num_blocks))) return nullptr;
        
        // 反序列化位图
        uint32_t bitmap_size;
        if (!safe_read(&bitmap_size, sizeof(bitmap_size))) return nullptr;
        
        size_t bitmap_bytes_size = (bitmap_size + 7) / 8;
        std::vector<uint8_t> bitmap_bytes(bitmap_bytes_size);
        if (!safe_read(bitmap_bytes.data(), bitmap_bytes_size)) return nullptr;
        
        // 重建 std::vector<bool>
        group.free_blocks.resize(bitmap_size);
        for (size_t j = 0; j < bitmap_size; ++j) {
            group.free_blocks[j] = (bitmap_bytes[j / 8] & (1 << (j % 8))) != 0;
        }
    }
    
    // 5. 重建辅助结构
    mgr->rebuild_auxiliary_structures();
    
    return mgr;
}

void BlockManager::rebuild_auxiliary_structures() {
    // 清空现有辅助结构
    alloc_map.clear();
    free_groups.clear();
    
    // 重建 free_groups 和 alloc_map
    for (size_t group_idx = 0; group_idx < groups_.size(); ++group_idx) {
        const auto& group = groups_[group_idx];
        
        // 如果组有足够的空闲块，加入 free_groups
        if (group.free_count >= group.num_blocks / 2) {
            free_groups.push_back(group_idx);
        }
        
        // 重建 alloc_map（为已分配的块创建默认条目）
        for (size_t block_idx = 0; block_idx < group.num_blocks; ++block_idx) {
            if (!group.free_blocks[block_idx]) {
                size_t global_block = group.first_block + block_idx;
                alloc_map[global_block] = {AllocType::DATA, false, 1};
            }
        }
    }
}