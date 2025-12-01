#include "VolumeManager.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
constexpr size_t kBytesPerBlock = BLOCK_SIZE;
}

void VolumeManager::register_volume(std::shared_ptr<Volume> volume,
                                    std::shared_ptr<IIOGateway> gateway) {
    if (!volume) {
        throw std::invalid_argument("VolumeManager::register_volume volume is null");
    }
    const std::string uuid = volume->uuid();
    VolumeContext ctx;
    ctx.volume = std::move(volume);
    ctx.gateway = std::move(gateway);
    volumes_[uuid] = std::move(ctx);
}

bool VolumeManager::set_volume_gateway(const std::string& volume_uuid,
                                       std::shared_ptr<IIOGateway> gateway) {
    auto it = volumes_.find(volume_uuid);
    if (it == volumes_.end()) {
        return false;
    }
    it->second.gateway = std::move(gateway);
    return true;
}

void VolumeManager::set_default_gateway(std::shared_ptr<IIOGateway> gateway) {
    default_gateway_ = std::move(gateway);
}

ssize_t VolumeManager::write_file(const std::shared_ptr<Inode>& inode,
                                  size_t offset,
                                  const char* buf,
                                  size_t count) {
    if (!inode || !buf || count == 0) {
        return 0;
    }
    auto ctx = resolve_context(inode->getVolumeUUID());
    if (!ctx || !ctx->volume) {
        std::cerr << "[VolumeManager] 未找到卷: " << inode->getVolumeUUID() << std::endl;
        return -1;
    }

    size_t total_blocks_needed = (offset + count + kBytesPerBlock - 1) / kBytesPerBlock;
    size_t bytes_allocated = 0;
    if (!ensure_blocks(inode, *ctx->volume, total_blocks_needed, bytes_allocated)) {
        return -1;
    }

    std::vector<IORequest> requests;
    requests.reserve(total_blocks_needed);

    size_t remaining = count;
    size_t current_offset = offset;
    const char* current_buf = buf;

    for (const auto& seg : inode->getBlocks()) {
        if (remaining == 0) break;

        const size_t seg_start_offset = seg.logical_start * kBytesPerBlock;
        const size_t seg_total_bytes  = seg.block_count * kBytesPerBlock;
        const size_t seg_end_offset   = seg_start_offset + seg_total_bytes;

        if (current_offset >= seg_end_offset) continue;
        if (current_offset + remaining <= seg_start_offset) break;

        size_t within_segment_offset = current_offset > seg_start_offset
            ? current_offset - seg_start_offset
            : 0;

        size_t block_index_in_seg = within_segment_offset / kBytesPerBlock;
        size_t block_inner_offset = within_segment_offset % kBytesPerBlock;

        size_t bytes_available = kBytesPerBlock - block_inner_offset;
        size_t bytes_to_write  = std::min(bytes_available, remaining);

        IORequest req{};
        req.type              = IOType::Write;
        req.storage_node_id   = ctx->volume->storage_node_id();
        req.node_id           = req.storage_node_id;
        req.volume_id         = inode->getVolumeUUID();
        req.volume_uuid       = req.volume_id;
        req.start_block       = seg.start_block + block_index_in_seg;
        req.block_count       = 1;
        req.offset_in_block   = block_inner_offset;
        req.offset            = req.offset_in_block;
        req.data_size         = bytes_to_write;
        req.length            = bytes_to_write;
        req.buffer            = const_cast<char*>(current_buf);
        req.buffer_size       = bytes_to_write;

        requests.emplace_back(req);

        current_offset += bytes_to_write;
        current_buf    += bytes_to_write;
        remaining      -= bytes_to_write;
    }

    if (remaining > 0) {
        std::cerr << "[VolumeManager] write_file 不足的物理块 (remaining="
                  << remaining << ")" << std::endl;
        return count - remaining;
    }

    dispatch_requests(*ctx, requests);

    inode->setFileSize(static_cast<uint64_t>(std::max<size_t>(
        inode->getFileSize(), offset + count)));
    return static_cast<ssize_t>(count);
}

ssize_t VolumeManager::read_file(const std::shared_ptr<Inode>& inode,
                                 size_t offset,
                                 char* buf,
                                 size_t count) {
    if (!inode || !buf || count == 0) {
        return 0;
    }
    auto ctx = resolve_context(inode->getVolumeUUID());
    if (!ctx || !ctx->volume) {
        std::cerr << "[VolumeManager] 未找到卷: " << inode->getVolumeUUID() << std::endl;
        return -1;
    }

    std::vector<IORequest> requests;
    requests.reserve(inode->getBlocks().size());

    size_t remaining = count;
    size_t current_offset = offset;
    char* current_buf = buf;

    for (const auto& seg : inode->getBlocks()) {
        if (remaining == 0) break;

        const size_t seg_start_offset = seg.logical_start * kBytesPerBlock;
        const size_t seg_total_bytes  = seg.block_count * kBytesPerBlock;
        const size_t seg_end_offset   = seg_start_offset + seg_total_bytes;

        if (current_offset >= seg_end_offset) continue;
        if (current_offset + remaining <= seg_start_offset) break;

        size_t within_segment_offset = current_offset > seg_start_offset
            ? current_offset - seg_start_offset
            : 0;

        size_t block_index_in_seg = within_segment_offset / kBytesPerBlock;
        size_t block_inner_offset = within_segment_offset % kBytesPerBlock;

        size_t bytes_available = kBytesPerBlock - block_inner_offset;
        size_t bytes_to_read   = std::min(bytes_available, remaining);

        IORequest req{};
        req.type        = IOType::Read;
        req.node_id     = inode->getVolumeUUID();
        req.volume_uuid = inode->getVolumeUUID();
        req.start_block = seg.start_block + block_index_in_seg;
        req.block_count = 1;
        req.offset      = block_inner_offset;
        req.length      = bytes_to_read;
        req.buffer      = current_buf;
        req.buffer_size = bytes_to_read;

        requests.emplace_back(req);

        current_offset += bytes_to_read;
        current_buf    += bytes_to_read;
        remaining      -= bytes_to_read;
    }

    if (remaining > 0) {
        std::memset(current_buf, 0, remaining);
    }

    dispatch_requests(*ctx, requests);
    return static_cast<ssize_t>(count);
}

bool VolumeManager::release_inode_blocks(const std::shared_ptr<Inode>& inode) {
    if (!inode) {
        return false;
    }
    auto ctx = resolve_context(inode->getVolumeUUID());
    if (!ctx || !ctx->volume) {
        std::cerr << "[VolumeManager] release_inode_blocks 未找到卷: "
                  << inode->getVolumeUUID() << std::endl;
        return false;
    }

    bool released = false;
    for (const auto& seg : inode->getBlocks()) {
        try {
            ctx->volume->safe_free_blocks(seg);
            released = true;
        } catch (const std::exception& ex) {
            std::cerr << "[VolumeManager] safe_free_blocks 失败: " << ex.what() << std::endl;
        }
    }
    if (released) {
        inode->clearBlocks();
        inode->setSizeUnit(0);
        inode->setFileSize(0);
    }
    return released;
}

VolumeManager::VolumeContext* VolumeManager::resolve_context(const std::string& volume_uuid) {
    auto it = volumes_.find(volume_uuid);
    if (it == volumes_.end()) {
        return nullptr;
    }
    if (!it->second.gateway && default_gateway_) {
        it->second.gateway = default_gateway_;
    }
    return &(it->second);
}

bool VolumeManager::ensure_blocks(const std::shared_ptr<Inode>& inode,
                                  Volume& volume,
                                  size_t total_blocks_needed,
                                  size_t& bytes_allocated) {
    size_t current_blocks = 0;
    for (const auto& seg : inode->getBlocks()) {
        current_blocks += seg.block_count;
    }
    if (total_blocks_needed <= current_blocks) {
        return true;
    }

    size_t to_allocate = total_blocks_needed - current_blocks;
    std::vector<BlockSegment> raw_segments = volume.allocate_blocks(AllocType::DATA, to_allocate);
    if (raw_segments.empty()) {
        std::cerr << "[VolumeManager] allocate_blocks 失败 (need=" << to_allocate << ")" << std::endl;
        return false;
    }

    size_t next_logical = 0;
    for (const auto& seg : inode->getBlocks()) {
        next_logical = std::max(next_logical, seg.logical_start + seg.block_count);
    }

    std::vector<BlockSegment> mapped;
    mapped.reserve(raw_segments.size());
    size_t remaining = to_allocate;

    for (const auto& seg : raw_segments) {
        size_t consumed = 0;
        while (consumed < seg.block_count && remaining > 0) {
            size_t chunk = std::min(seg.block_count - consumed, remaining);
            mapped.emplace_back(next_logical,
                                seg.start_block + consumed,
                                chunk);
            next_logical += chunk;
            consumed += chunk;
            remaining -= chunk;
        }
        if (remaining == 0) break;
    }

    inode->appendBlocks(mapped);
    bytes_allocated = to_allocate * kBytesPerBlock;
    return true;
}

void VolumeManager::dispatch_requests(VolumeContext& ctx,
                                      std::vector<IORequest>& requests) {
    if (requests.empty()) return;

    if (ctx.gateway) {
        ctx.gateway->processIOBatch(requests);
    } else if (ctx.volume) {
        ctx.volume->submit_io_requests(requests);
    } else {
        throw std::runtime_error("VolumeManager: neither gateway nor volume available");
    }
}