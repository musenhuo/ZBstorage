#pragma once
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>
#include "../../fs/volume/VolumeRegistry.h"
#include "../../fs/volume/Volume.h"
#include "../inode/inode.h"

/**
 * @brief VolumeAllocator 提供在 MDS 端对 inode 进行卷绑定与块释放的能力。
 *        该组件接受一个 IVolumeRegistry 指针，通过查询已注册的卷
 *        来为目录/文件选择合适的卷，并在删除/截断时释放物理块。
 */
class VolumeAllocator {
public:
    explicit VolumeAllocator(std::shared_ptr<IVolumeRegistry> registry);

    /**
     * @brief 为 inode 选择一个卷并写入 inode->volume_id。
     * @param inode 目标 inode（可能为目录或普通文件）。
     * @return true 表示已成功绑定一个卷（volume_id 不为空），false 表示未能分配。
     */
    bool allocate_for_inode(const std::shared_ptr<Inode>& inode);

    /**
     * @brief 释放 inode 当前记录的所有块（通过 Volume::free_blocks）。
     * @param inode 目标 inode（其 block_segments 表述需被释放）。
     * @return 释放是否成功（尽可能释放能找到的卷上的块，单个失败不影响其它块释放）。
     */
    bool free_blocks_for_inode(const std::shared_ptr<Inode>& inode);

private:
    std::shared_ptr<IVolumeRegistry> registry_;
    static constexpr size_t kReserveBlocks = 128; // 预留阈值

    struct CandidateState {
        std::weak_ptr<Volume> volume;
        size_t last_free{ 0 };
        uint64_t ticket{ 0 };
    };

    struct Candidate {
        std::string uuid;
        uint64_t ticket;
        size_t cached_free;
    };

    struct CandidateCompare {
        bool operator()(const Candidate& lhs, const Candidate& rhs) const {
            if (lhs.cached_free == rhs.cached_free) {
                return lhs.uuid > rhs.uuid;
            }
            return lhs.cached_free < rhs.cached_free;
        }
    };

    struct VolumePool {
        std::priority_queue<Candidate, std::vector<Candidate>, CandidateCompare> heap;
        std::unordered_map<std::string, CandidateState> states;
        std::chrono::steady_clock::time_point last_refresh{ std::chrono::steady_clock::now() };
    };

    VolumePool ssd_pool_;
    VolumePool hdd_pool_;
    std::unordered_map<std::string, VolumeType> volume_type_index_;
    uint64_t ticket_counter_{ 0 };
    mutable std::mutex mu_;

    VolumePool& pool_for(VolumeType type);
    const VolumePool& pool_for(VolumeType type) const;
    bool refresh_pool(VolumeType type, bool force);
    std::shared_ptr<Volume> pick_volume(VolumeType type);
    std::vector<std::shared_ptr<Volume>> collect_active_volumes();
    void record_volume_usage(const std::shared_ptr<Volume>& volume,
                             VolumeType type,
                             size_t free_blocks);
    std::pair<std::shared_ptr<Volume>, std::optional<VolumeType>> resolve_volume(const std::string& uuid);
    std::optional<VolumeType> ensure_volume_type(const std::string& uuid);
    void cleanup_pool(VolumeType type);
};
