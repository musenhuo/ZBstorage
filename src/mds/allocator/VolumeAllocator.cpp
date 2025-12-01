#include "VolumeAllocator.h"
#include <iostream>

namespace {
constexpr auto kMinRefreshInterval = std::chrono::milliseconds(200);
}

VolumeAllocator::VolumeAllocator(std::shared_ptr<IVolumeRegistry> registry)
    : registry_(std::move(registry)) {}

VolumeAllocator::VolumePool& VolumeAllocator::pool_for(VolumeType type) {
    return (type == VolumeType::SSD) ? ssd_pool_ : hdd_pool_;
}

const VolumeAllocator::VolumePool& VolumeAllocator::pool_for(VolumeType type) const {
    return (type == VolumeType::SSD) ? ssd_pool_ : hdd_pool_;
}

void VolumeAllocator::record_volume_usage(const std::shared_ptr<Volume>& volume,
                                          VolumeType type,
                                          size_t free_blocks) {
    if (!volume) return;
    std::lock_guard<std::mutex> lk(mu_);
    auto& pool = pool_for(type);
    auto& state = pool.states[volume->uuid()];
    state.volume = volume;
    state.last_free = free_blocks;
    state.ticket = ++ticket_counter_;
    volume_type_index_[volume->uuid()] = type;
    if (free_blocks > kReserveBlocks) {
        pool.heap.push(Candidate{ volume->uuid(), state.ticket, free_blocks });
    }
}

void VolumeAllocator::cleanup_pool(VolumeType type) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& pool = pool_for(type);
    for (auto it = pool.states.begin(); it != pool.states.end();) {
        if (it->second.volume.expired()) {
            volume_type_index_.erase(it->first);
            it = pool.states.erase(it);
        } else {
            ++it;
        }
    }
}

bool VolumeAllocator::refresh_pool(VolumeType type, bool force) {
    if (!registry_) return false;
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto& pool = pool_for(type);
        if (!force && (now - pool.last_refresh) < kMinRefreshInterval && !pool.heap.empty()) {
            return true;
        }
        pool.last_refresh = now;
    }

    const auto& vols = registry_->list(type);
    bool any = false;
    for (const auto& vol : vols) {
        if (!vol) continue;
        auto free_blocks = vol->block_manager().free_blocks_count();
        record_volume_usage(vol, type, free_blocks);
        any = true;
    }
    cleanup_pool(type);
    return any;
}

std::shared_ptr<Volume> VolumeAllocator::pick_volume(VolumeType type) {
    if (!registry_) return nullptr;
    for (int refresh_attempt = 0; refresh_attempt < 2; ++refresh_attempt) {
        std::unique_lock<std::mutex> lk(mu_);
        auto& pool = pool_for(type);
        const size_t max_attempts = pool.states.size();
        size_t attempts = 0;
        while (!pool.heap.empty() && (max_attempts == 0 || attempts < max_attempts)) {
            Candidate cand = pool.heap.top();
            pool.heap.pop();
            ++attempts;
            auto state_it = pool.states.find(cand.uuid);
            if (state_it == pool.states.end()) continue;
            if (state_it->second.ticket != cand.ticket) continue;
            auto volume = state_it->second.volume.lock();
            if (!volume) {
                volume_type_index_.erase(cand.uuid);
                pool.states.erase(state_it);
                continue;
            }
            size_t free_blocks = volume->block_manager().free_blocks_count();
            if (free_blocks > kReserveBlocks) {
                state_it->second.last_free = free_blocks;
                state_it->second.ticket = ++ticket_counter_;
                pool.heap.push(Candidate{ cand.uuid, state_it->second.ticket, free_blocks });
                lk.unlock();
                return volume;
            }
            state_it->second.last_free = free_blocks;
            state_it->second.ticket = ++ticket_counter_;
        }
        lk.unlock();
        if (!refresh_pool(type, true)) {
            break;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<Volume>> VolumeAllocator::collect_active_volumes() {
    std::vector<std::shared_ptr<Volume>> result;
    std::lock_guard<std::mutex> lk(mu_);
    auto collect = [&result](const VolumePool& pool) {
        for (const auto& [uuid, state] : pool.states) {
            if (auto vol = state.volume.lock()) {
                result.push_back(vol);
            }
        }
    };
    collect(ssd_pool_);
    collect(hdd_pool_);
    return result;
}

std::optional<VolumeType> VolumeAllocator::ensure_volume_type(const std::string& uuid) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = volume_type_index_.find(uuid);
        if (it != volume_type_index_.end()) {
            return it->second;
        }
    }
    if (refresh_pool(VolumeType::SSD, true)) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = volume_type_index_.find(uuid);
        if (it != volume_type_index_.end()) return it->second;
    }
    if (refresh_pool(VolumeType::HDD, true)) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = volume_type_index_.find(uuid);
        if (it != volume_type_index_.end()) return it->second;
    }
    return std::nullopt;
}

std::pair<std::shared_ptr<Volume>, std::optional<VolumeType>>
VolumeAllocator::resolve_volume(const std::string& uuid) {
    if (uuid.empty()) return { nullptr, std::nullopt };
    if (!registry_) return { nullptr, std::nullopt };

    if (auto vol = registry_->find_by_uuid(uuid)) {
        auto type_opt = ensure_volume_type(uuid);
        if (type_opt) {
            auto free_blocks = vol->block_manager().free_blocks_count();
            record_volume_usage(vol, *type_opt, free_blocks);
        }
        return { vol, type_opt };
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = volume_type_index_.find(uuid);
        if (it != volume_type_index_.end()) {
            const auto& pool = pool_for(it->second);
            auto state_it = pool.states.find(uuid);
            if (state_it != pool.states.end()) {
                if (auto vol = state_it->second.volume.lock()) {
                    return { vol, it->second };
                }
            }
        }
    }
    return { nullptr, std::nullopt };
}

bool VolumeAllocator::allocate_for_inode(const std::shared_ptr<Inode>& inode) {
    if (!inode || !registry_) return false;
    auto pick = [this](VolumeType type) { return pick_volume(type); };

    std::shared_ptr<Volume> chosen;
    if (inode->file_mode.fields.file_type == static_cast<uint8_t>(FileType::Directory)) {
        chosen = pick(VolumeType::SSD);
        if (!chosen) chosen = pick(VolumeType::HDD);
    } else {
        chosen = pick(VolumeType::SSD);
        if (!chosen) chosen = pick(VolumeType::HDD);
    }

    if (!chosen) {
        inode->setVolumeId(std::string());
        return false;
    }
    inode->setVolumeId(chosen->uuid());
    return true;
}

bool VolumeAllocator::free_blocks_for_inode(const std::shared_ptr<Inode>& inode) {
    if (!inode || !registry_) return false;
    bool any_done = false;
    const auto& blocks = inode->getBlocks();
    if (blocks.empty()) return false;

    auto vol_uuid = inode->getVolumeUUID();
    if (!vol_uuid.empty()) {
        auto [volume, type_opt] = resolve_volume(vol_uuid);
        if (volume) {
            for (const auto& seg : blocks) {
                try {
                    volume->free_blocks(seg);
                    any_done = true;
                } catch (const std::exception& e) {
                    std::cerr << "[VolumeAllocator] free_blocks failed: " << e.what() << std::endl;
                }
            }
            if (any_done && type_opt) {
                auto free_blocks = volume->block_manager().free_blocks_count();
                record_volume_usage(volume, *type_opt, free_blocks);
            }
            return any_done;
        }
    }

    auto candidates = collect_active_volumes();
    if (candidates.empty()) {
        refresh_pool(VolumeType::SSD, true);
        refresh_pool(VolumeType::HDD, true);
        candidates = collect_active_volumes();
    }

    for (const auto& volume : candidates) {
        if (!volume) continue;
        for (const auto& seg : blocks) {
            try {
                volume->free_blocks(seg);
                any_done = true;
            } catch (...) {
                // 忽略单卷失败，继续尝试其它卷
            }
        }
        if (any_done) {
            if (auto type_opt = ensure_volume_type(volume->uuid())) {
                auto free_blocks = volume->block_manager().free_blocks_count();
                record_volume_usage(volume, *type_opt, free_blocks);
            }
        }
    }
    return any_done;
}
