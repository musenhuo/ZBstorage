#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace mds {

enum class DirectoryLockMode {
    kShared,
    kExclusive
};

class DirectoryLockTable {
public:
    explicit DirectoryLockTable(size_t shard_count = 0);
    DirectoryLockTable(const DirectoryLockTable&) = delete;
    DirectoryLockTable& operator=(const DirectoryLockTable&) = delete;

    std::shared_ptr<std::shared_mutex> Acquire(uint64_t inode);

private:
    struct Segment {
        std::mutex mu;
        std::unordered_map<uint64_t, std::weak_ptr<std::shared_mutex>> locks;
    };

    std::vector<std::unique_ptr<Segment>> segments_;

    static size_t DefaultShardCount();
    std::shared_ptr<std::shared_mutex> AcquireFromSegment(Segment& segment, uint64_t inode);
};

class DirectoryLockGuard {
public:
    DirectoryLockGuard(DirectoryLockTable& table, uint64_t inode, DirectoryLockMode mode);
    DirectoryLockGuard(DirectoryLockGuard&& other) noexcept;
    DirectoryLockGuard& operator=(DirectoryLockGuard&& other) noexcept;
    ~DirectoryLockGuard();

    DirectoryLockGuard(const DirectoryLockGuard&) = delete;
    DirectoryLockGuard& operator=(const DirectoryLockGuard&) = delete;

private:
    DirectoryLockMode mode_{ DirectoryLockMode::kShared };
    std::shared_ptr<std::shared_mutex> lock_;
    std::shared_lock<std::shared_mutex> shared_lock_;
    std::unique_lock<std::shared_mutex> unique_lock_;
};

} // namespace mds
