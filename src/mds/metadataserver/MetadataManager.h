// MetadataManager.h: inode 与位图的分配与持久化管理（声明）
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <limits>
#include "../../debug/ZBLog.h"
#include <stdexcept>

#include <boost/dynamic_bitset.hpp>

#include "../inode/InodeStorage.h" // 提供 InodeStorage 与 BitmapStorage 的声明
#include "metadataserver/KVStore.h"

// 若工程中已定义以下宏，请忽略这里的占位默认值
// inode分配位图存储路径
// constexpr const char* INODE_BITMAP_PATH = "/mnt/md0/node/inode_bitmap.bin";
constexpr const char* INODE_BITMAP_PATH = "/mnt/nvme/node/inode_bitmap.bin";
// inode 存储路径
// constexpr const char* INODE_STORAGE_PATH = "/mnt/md0/node/inode_storage.bin";
constexpr const char* INODE_STORAGE_PATH = "/mnt/nvme/node/inode_storage.bin";

// --- MetadataManager: 管理 inode 和位图的分配与持久化 ---
class MetadataManager {
private:
	std::shared_ptr<InodeStorage> inode_storage;
	std::shared_ptr<BitmapStorage> bitmap_storage;
	boost::dynamic_bitset<> inode_bitmap;
	uint64_t total_inodes = 0;
	std::mutex mtx;
	size_t start_inodeno = 2;
	uint64_t next_free_hint_ = 2;
	static constexpr size_t kBitmapBlockBytes = 4096;
	static constexpr uint64_t kInvalidInode = std::numeric_limits<uint64_t>::max();
	std::vector<uint8_t> bitmap_dirty_blocks_;
	std::vector<char> bitmap_block_buffer_;

	// 可选：KV 后端（用于以 key->inode 存储元数据）
	std::unique_ptr<mds::KVStore> kv_store_;
	
public:
	// 构造函数，分别指定 inode 文件和位图文件路径
	// 增加可选参数 use_kv 与 kv_path
	// By default enable KV-backed path->inode mapping (use_kv=true).
	MetadataManager(const std::string& inode_file_path = INODE_STORAGE_PATH,
				const std::string& bitmap_file_path = INODE_BITMAP_PATH,
				bool create_new = false,
				size_t start_inodeno = 2,
				bool use_kv = true,
				const std::string& kv_path = "/tmp/zbstorage_kv");

	// 分配新 inode
	uint64_t allocate_inode(mode_t mode);

	std::shared_ptr<InodeStorage> get_inode_storage() const;

	// 新增：返回当前位图记录的总 inode 槽数
	uint64_t get_total_inodes() const;

	// 新增：判断inode是否已分配（安全读取）
	bool is_inode_allocated(uint64_t ino);

	// Path -> Inode mapping (KV-backed index)
	// Put a mapping from path to a serialized Inode
	bool put_inode_for_path(const std::string& path, const ::Inode& inode);

	// Get Inode by path; returns nullopt if not found or KV not enabled
	std::optional<::Inode> get_inode_by_path(const std::string& path) const;

	// Delete mapping for path (if KV enabled)
	bool delete_inode_path(const std::string& path);

	// 持久化位图到 bitmap 文件
	void save_bitmap();
	void mark_inode_free(uint64_t ino);

private:
	// 加载位图
	void load_bitmap();

	// 标记 inode 已分配并持久化
	uint64_t mark_inode_used(uint64_t ino, mode_t mode);
	uint64_t allocate_from_index(uint64_t idx, mode_t mode);
	uint64_t find_free_slot(uint64_t start) const;
	void advance_next_hint(uint64_t last_allocated);
	void ensure_dirty_tracking();
	void mark_bitmap_block_dirty(uint64_t ino);
	void mark_all_bitmap_blocks_dirty();
	void mark_bitmap_range_dirty(uint64_t bit_offset, uint64_t bit_length);
	void flush_dirty_bitmap_blocks();
	size_t bitmap_block_count() const;
	uint64_t refresh_next_hint();

	// 扩展 inode 文件和位图
	uint64_t expand_and_allocate(mode_t mode, size_t start_inodeno);
};

