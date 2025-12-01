// MetadataManager.cpp: inode 与位图的分配与持久化管理（定义）
#include "MetadataManager.h"
#include <algorithm>
#include <iostream>

#include "KVStore.h"

#include <arpa/inet.h>
#include <cstring>

// fixed namespace id for keys (temporary constant until namespace concept added)
static const uint64_t kNamespaceId = 1;

// helper: convert uint64 to network byte order (big-endian)
static uint64_t htonll(uint64_t x) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
	return (((uint64_t)htonl(static_cast<uint32_t>(x & 0xffffffffULL))) << 32) | htonl(static_cast<uint32_t>(x >> 32));
#else
	return x;
#endif
}

// FNV-1a 64-bit hash for strings (simple, portable)
static uint64_t fnv1a64(const char* data, size_t len) {
	const uint64_t FNV_offset = 14695981039346656037ULL;
	const uint64_t FNV_prime = 1099511628211ULL;
	uint64_t h = FNV_offset;
	const unsigned char* ptr = reinterpret_cast<const unsigned char*>(data);
	for (size_t i = 0; i < len; ++i) {
		h ^= static_cast<uint64_t>(ptr[i]);
		h *= FNV_prime;
	}
	return h;
}

// normalize path: collapse multiple slashes, remove trailing slash (except root)
static std::string normalize_path(const std::string& p) {
	if (p.empty()) return p;
	std::string out;
	out.reserve(p.size());
	bool last_slash = false;
	for (char c : p) {
		if (c == '/') {
			if (!last_slash) { out.push_back('/'); last_slash = true; }
		} else {
			out.push_back(c);
			last_slash = false;
		}
	}
	if (out.size() > 1 && out.back() == '/') out.pop_back();
	return out;
}

// generate 24-byte ID: [8B uid_be][8B hash_parent_be][8B hash_filename_be]
static std::string generateID_for_path(const std::string& filepath, uint64_t uid) {
	std::string p = normalize_path(filepath);
	size_t pos = p.find_last_of('/');
	std::string parent = (pos == std::string::npos) ? std::string("") : p.substr(0, pos);
	// special: parent for "/name" should be "/"
	if (parent.empty()) parent = std::string("/");
	std::string filename = (pos == std::string::npos) ? p : p.substr(pos + 1);

	uint64_t hp = fnv1a64(parent.c_str(), parent.size());
	uint64_t hf = fnv1a64(filename.c_str(), filename.size());

	uint64_t uid_be = htonll(uid);
	uint64_t hp_be = htonll(hp);
	uint64_t hf_be = htonll(hf);

	std::string result;
	result.resize(24);
	std::memcpy(&result[0], &uid_be, 8);
	std::memcpy(&result[8], &hp_be, 8);
	std::memcpy(&result[16], &hf_be, 8);
	return result;
}

MetadataManager::MetadataManager(const std::string& inode_file_path,
																 const std::string& bitmap_file_path,
																 bool create_new,
																 size_t start_inodeno_,
																 bool use_kv,
																 const std::string& kv_path)
		: inode_storage(std::make_shared<InodeStorage>(inode_file_path, create_new)),
			bitmap_storage(std::make_shared<BitmapStorage>(bitmap_file_path, create_new)),
			start_inodeno(start_inodeno_),
			next_free_hint_(start_inodeno_) {
		if (use_kv) {
				kv_store_ = std::make_unique<mds::KVStore>(kv_path);
		}
	// 加载位图
	if (!create_new) {
		load_bitmap();
	} else {
		total_inodes = inode_bitmap.size();
	}
	ensure_dirty_tracking();
	next_free_hint_ = refresh_next_hint();
}

uint64_t MetadataManager::allocate_inode(mode_t mode) {
	std::lock_guard<std::mutex> lock(mtx);
	uint64_t slot = find_free_slot(next_free_hint_);
	if (slot == kInvalidInode) {
		return expand_and_allocate(mode, start_inodeno);
	}
	return allocate_from_index(slot, mode);
}

std::shared_ptr<InodeStorage> MetadataManager::get_inode_storage() const {
	return inode_storage;
}

uint64_t MetadataManager::get_total_inodes() const {
	return total_inodes;
}

bool MetadataManager::is_inode_allocated(uint64_t ino) {
	std::lock_guard<std::mutex> lock(mtx);
	if (ino >= inode_bitmap.size()) return false;
	return inode_bitmap.test(ino);
}

void MetadataManager::save_bitmap() {
	flush_dirty_bitmap_blocks();
}

void MetadataManager::load_bitmap() {
	std::cout << "Loading inode bitmap..." << std::endl;
	std::vector<char> bitmap_data((total_inodes + 7) / 8, 0);
	bitmap_storage->read_bitmap(bitmap_data);
	inode_bitmap = boost::dynamic_bitset<>(bitmap_data.size() * 8);
	for (size_t i = 0; i < bitmap_data.size() * 8; ++i) {
		if (bitmap_data[i / 8] & (1 << (i % 8))) {
			inode_bitmap.set(i);
		}
	}
	std::cout << "Loaded inode bitmap with size: " << inode_bitmap.size() << std::endl;
	total_inodes = inode_bitmap.size();
}

uint64_t MetadataManager::mark_inode_used(uint64_t ino, mode_t /*mode*/) {
	inode_bitmap.set(ino);
	mark_bitmap_block_dirty(ino);
	// 持久化初始化 inode：写入 KV（现在默认启用 KV 后端），同时保留写入 InodeStorage 的可能性。
	// 使用 key = "inode:<ino>" 存储序列化的 Inode（便于按 inode 查询）。
	::Inode dinode;
	dinode.inode = ino;
	std::string key = std::string("inode:") + std::to_string(ino);
	if (kv_store_) {
		kv_store_->put(key, dinode);
	}
	// 仍可选择写入 inode_storage 以兼容需要的持久化路径（未启用默认写入）
	return ino;
}

uint64_t MetadataManager::expand_and_allocate(mode_t mode, size_t start_inodeno_) {
	const uint64_t chunk_size = 65536;
	uint64_t old_total = total_inodes;
	total_inodes += chunk_size;
	inode_bitmap.resize(total_inodes);
	inode_storage->expand(total_inodes * InodeStorage::INODE_DISK_SLOT_SIZE);
	ensure_dirty_tracking();
	mark_bitmap_range_dirty(old_total, chunk_size);
	save_bitmap();
	next_free_hint_ = std::max<uint64_t>(start_inodeno_, old_total);
	uint64_t slot = find_free_slot(next_free_hint_);
	if (slot == kInvalidInode) {
		throw std::runtime_error("expand_and_allocate failed to find free inode!");
	}
	return allocate_from_index(slot, mode);
}

uint64_t MetadataManager::allocate_from_index(uint64_t idx, mode_t mode) {
	mark_inode_used(idx, mode);
	advance_next_hint(idx);
	save_bitmap();
	return idx;
}

uint64_t MetadataManager::find_free_slot(uint64_t start) const {
	if (inode_bitmap.empty()) {
		return kInvalidInode;
	}
	uint64_t limit = inode_bitmap.size();
	if (start >= limit) start = start_inodeno;
	for (uint64_t i = start; i < limit; ++i) {
		if (!inode_bitmap.test(i)) {
			return i;
		}
	}
	for (uint64_t i = start_inodeno; i < start && i < limit; ++i) {
		if (!inode_bitmap.test(i)) {
			return i;
		}
	}
	return kInvalidInode;
}

void MetadataManager::advance_next_hint(uint64_t last_allocated) {
	uint64_t next = last_allocated + 1;
	if (next >= inode_bitmap.size()) {
		next = start_inodeno;
	}
	next_free_hint_ = next;
}

void MetadataManager::ensure_dirty_tracking() {
	size_t blocks = bitmap_block_count();
	if (bitmap_dirty_blocks_.size() < blocks) {
		bitmap_dirty_blocks_.resize(blocks, 0);
	} else if (bitmap_dirty_blocks_.size() > blocks) {
		bitmap_dirty_blocks_.resize(blocks);
	}
	if (bitmap_block_buffer_.size() < kBitmapBlockBytes) {
		bitmap_block_buffer_.assign(kBitmapBlockBytes, 0);
	}
}

size_t MetadataManager::bitmap_block_count() const {
	const size_t bits_per_block = kBitmapBlockBytes * 8;
	if (bits_per_block == 0) return 0;
	return (inode_bitmap.size() + bits_per_block - 1) / bits_per_block;
}

void MetadataManager::mark_bitmap_block_dirty(uint64_t ino) {
	const size_t bits_per_block = kBitmapBlockBytes * 8;
	if (bits_per_block == 0) return;
	const size_t block = static_cast<size_t>(ino) / bits_per_block;
	if (block >= bitmap_dirty_blocks_.size()) {
		bitmap_dirty_blocks_.resize(block + 1, 0);
	}
	bitmap_dirty_blocks_[block] = 1;
}

void MetadataManager::mark_all_bitmap_blocks_dirty() {
	mark_bitmap_range_dirty(0, inode_bitmap.size());
}

void MetadataManager::mark_bitmap_range_dirty(uint64_t bit_offset, uint64_t bit_length) {
	const size_t bits_per_block = kBitmapBlockBytes * 8;
	if (bits_per_block == 0 || bit_length == 0) return;
	uint64_t start_block = bit_offset / bits_per_block;
	uint64_t end_block = (bit_offset + bit_length + bits_per_block - 1) / bits_per_block;
	if (end_block > bitmap_dirty_blocks_.size()) {
		bitmap_dirty_blocks_.resize(static_cast<size_t>(end_block), 0);
	}
	for (uint64_t blk = start_block; blk < end_block; ++blk) {
		bitmap_dirty_blocks_[static_cast<size_t>(blk)] = 1;
	}
}

void MetadataManager::flush_dirty_bitmap_blocks() {
	const size_t bits_per_block = kBitmapBlockBytes * 8;
	if (!bitmap_storage || bits_per_block == 0 || bitmap_dirty_blocks_.empty()) return;
	bool any_flushed = false;
	for (size_t block = 0; block < bitmap_dirty_blocks_.size(); ++block) {
		if (!bitmap_dirty_blocks_[block]) continue;
		size_t bit_offset = block * bits_per_block;
		if (bit_offset >= inode_bitmap.size()) {
			bitmap_dirty_blocks_[block] = 0;
			continue;
		}
		size_t remaining_bits = inode_bitmap.size() - bit_offset;
		size_t bit_count = std::min(remaining_bits, bits_per_block);
		size_t byte_count = (bit_count + 7) / 8;
		std::fill(bitmap_block_buffer_.begin(), bitmap_block_buffer_.begin() + byte_count, 0);
		for (size_t i = 0; i < bit_count; ++i) {
			if (inode_bitmap.test(bit_offset + i)) {
				bitmap_block_buffer_[i / 8] |= static_cast<char>(1 << (i % 8));
			}
		}
		if (bitmap_storage->write_bitmap_region(block * kBitmapBlockBytes,
				bitmap_block_buffer_.data(), byte_count)) {
			bitmap_dirty_blocks_[block] = 0;
			any_flushed = true;
		}
	}
	if (any_flushed) {
		LOGD("[WRITE] 位图增量刷新完成");
	}
}

uint64_t MetadataManager::refresh_next_hint() {
	uint64_t hint = find_free_slot(start_inodeno);
	return hint == kInvalidInode ? start_inodeno : hint;
}

void MetadataManager::mark_inode_free(uint64_t ino) {
	std::lock_guard<std::mutex> lock(mtx);
	if (ino >= inode_bitmap.size()) return;
	inode_bitmap.reset(ino);
	mark_bitmap_block_dirty(ino);
	if (ino < next_free_hint_) {
		next_free_hint_ = ino;
	}
	save_bitmap();
}

bool MetadataManager::put_inode_for_path(const std::string& path, const ::Inode& inode) {
	if (!kv_store_) return false;
	// generate 24-byte binary key
	std::string key = generateID_for_path(path, kNamespaceId);
	// build value: 4-byte path length (network order) + path bytes + inode bytes
	std::vector<uint8_t> value;
	auto inode_bytes = inode.serialize();
	uint32_t path_len = static_cast<uint32_t>(path.size());
	uint32_t path_len_be = htonl(path_len);
	value.resize(4 + path_len + inode_bytes.size());
	std::memcpy(value.data(), &path_len_be, 4);
	std::memcpy(value.data() + 4, path.data(), path_len);
	std::memcpy(value.data() + 4 + path_len, inode_bytes.data(), inode_bytes.size());
	return kv_store_->put_raw(key, value);
}

std::optional<::Inode> MetadataManager::get_inode_by_path(const std::string& path) const {
	if (!kv_store_) return std::nullopt;
	std::string key = generateID_for_path(path, kNamespaceId);
	auto got = kv_store_->get_raw(key);
	if (!got) return std::nullopt;
	const std::vector<uint8_t>& buf = *got;
	if (buf.size() < 4) return std::nullopt;
	uint32_t path_len_be = 0;
	std::memcpy(&path_len_be, buf.data(), 4);
	uint32_t path_len = ntohl(path_len_be);
	if (buf.size() < 4 + path_len) return std::nullopt;
	std::string stored_path(reinterpret_cast<const char*>(buf.data() + 4), path_len);
	// collision check: ensure stored path equals requested path
	if (stored_path != path) return std::nullopt;
	size_t offset = 4 + path_len;
	size_t remaining = buf.size() - offset;
	size_t off = 0;
	::Inode out;
	if (!::Inode::deserialize(buf.data() + offset, off, out, remaining)) return std::nullopt;
	return out;
}

bool MetadataManager::delete_inode_path(const std::string& path) {
	if (!kv_store_) return false;
	std::string key = generateID_for_path(path, kNamespaceId);
	return kv_store_->del_raw(key);
}

