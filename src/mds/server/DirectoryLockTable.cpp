#include "DirectoryLockTable.h"

#include <algorithm>
#include <thread>

namespace mds {

DirectoryLockTable::DirectoryLockTable(size_t shard_count) {
    size_t count = shard_count;
    if (count == 0) {
        count = DefaultShardCount();
    }
    segments_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        segments_.emplace_back(std::make_unique<Segment>());
    }
}

std::shared_ptr<std::shared_mutex> DirectoryLockTable::Acquire(uint64_t inode) {
    if (segments_.empty()) {
        segments_.emplace_back(std::make_unique<Segment>());
    }
    auto& segment = *segments_[inode % segments_.size()];
    return AcquireFromSegment(segment, inode);
}

size_t DirectoryLockTable::DefaultShardCount() {
    auto hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 8;
    return static_cast<size_t>(std::max<uint32_t>(64, hw * 16));
}

std::shared_ptr<std::shared_mutex> DirectoryLockTable::AcquireFromSegment(Segment& segment, uint64_t inode) {
    std::lock_guard<std::mutex> lk(segment.mu);
    auto it = segment.locks.find(inode);
    if (it != segment.locks.end()) {
        if (auto existing = it->second.lock()) {
            return existing;
        }
        segment.locks.erase(it);
    }
    auto created = std::make_shared<std::shared_mutex>();
    segment.locks.emplace(inode, created);
    return created;
}

DirectoryLockGuard::DirectoryLockGuard(DirectoryLockTable& table,
                                       uint64_t inode,
                                       DirectoryLockMode mode)
    : mode_(mode),
      lock_(table.Acquire(inode)) {
    if (!lock_) {
        return;
    }
    if (mode_ == DirectoryLockMode::kShared) {
        shared_lock_ = std::shared_lock<std::shared_mutex>(*lock_);
    } else {
        unique_lock_ = std::unique_lock<std::shared_mutex>(*lock_);
    }
}

DirectoryLockGuard::DirectoryLockGuard(DirectoryLockGuard&& other) noexcept
    : mode_(other.mode_),
      lock_(std::move(other.lock_)),
      shared_lock_(std::move(other.shared_lock_)),
      unique_lock_(std::move(other.unique_lock_)) {}

DirectoryLockGuard& DirectoryLockGuard::operator=(DirectoryLockGuard&& other) noexcept {
    if (this == &other) return *this;
    mode_ = other.mode_;
    lock_ = std::move(other.lock_);
    shared_lock_ = std::move(other.shared_lock_);
    unique_lock_ = std::move(other.unique_lock_);
    return *this;
}

DirectoryLockGuard::~DirectoryLockGuard() = default;

} // namespace mds
