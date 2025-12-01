
#include "KVStore.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>

namespace fs = std::filesystem;
using namespace mds;

static std::string hash_to_hex(size_t h) {
    std::ostringstream oss;
    oss << std::hex << std::setw(sizeof(size_t) * 2) << std::setfill('0') << h;
    return oss.str();
}

std::string KVStore::key_to_path(const std::string& key) const {
    size_t h = std::hash<std::string>{}(key);
    return (fs::path(base_dir_) / (hash_to_hex(h) + ".kv")).string();
}

#ifdef USE_ROCKSDB
// RocksDB backend
#include <rocksdb/db.h>
#include <rocksdb/options.h>

class RocksDBWrapper {
public:
    RocksDBWrapper() = default;
    ~RocksDBWrapper() { close(); }

    bool open(const std::string& path) {
        rocksdb::Options options;
        options.create_if_missing = true;
        rocksdb::Status s = rocksdb::DB::Open(options, path, &db_);
        return s.ok();
    }

    void close() { if (db_) { delete db_; db_ = nullptr; } }

    bool put(const std::string& key, const std::string& value) {
        if (!db_) return false;
        rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), key, value);
        return s.ok();
    }

    bool get(const std::string& key, std::string& value) {
        if (!db_) return false;
        rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), key, &value);
        return s.ok();
    }

    bool del(const std::string& key) {
        if (!db_) return false;
        rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), key);
        return s.ok();
    }

    bool put_raw(const std::string& key, const std::string& value) {
        return put(key, value);
    }

    bool get_raw(const std::string& key, std::string& value) {
        return get(key, value);
    }

private:
    rocksdb::DB* db_ = nullptr;
};

// single shared RocksDB instance for this simple demo wrapper
static RocksDBWrapper s_rocksdb;

KVStore::KVStore(const std::string& base_dir) : base_dir_(base_dir) {
    std::error_code ec;
    fs::create_directories(base_dir_, ec);
    s_rocksdb.open(base_dir_);
}

bool KVStore::put(const std::string& key, const ::Inode& value) {
    auto buf = value.serialize();
    std::string v;
    v.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
    return s_rocksdb.put(key, v);
}

std::optional<::Inode> KVStore::get(const std::string& key) {
    std::string v;
    if (!s_rocksdb.get(key, v)) return std::nullopt;
    std::vector<uint8_t> buf(v.begin(), v.end());
    size_t offset = 0;
    ::Inode out;
    if (!::Inode::deserialize(buf.data(), offset, out, buf.size())) return std::nullopt;
    return out;
}

bool KVStore::del(const std::string& key) {
    return s_rocksdb.del(key);
}

bool KVStore::put_raw(const std::string& key, const std::vector<uint8_t>& data) {
    std::string v(reinterpret_cast<const char*>(data.data()), data.size());
    return s_rocksdb.put_raw(key, v);
}

std::optional<std::vector<uint8_t>> KVStore::get_raw(const std::string& key) {
    std::string v;
    if (!s_rocksdb.get_raw(key, v)) return std::nullopt;
    return std::vector<uint8_t>(v.begin(), v.end());
}

bool KVStore::del_raw(const std::string& key) {
    return s_rocksdb.del(key);
}

#else
// Fallback file-backed backend (same behavior as previous simple implementation)

KVStore::KVStore(const std::string& base_dir) : base_dir_(base_dir) {
    std::error_code ec;
    fs::create_directories(base_dir_, ec);
}

bool KVStore::put(const std::string& key, const ::Inode& value) {
    auto buf = value.serialize();
    std::string path = key_to_path(key);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    if (!buf.empty()) {
        ofs.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    }
    return ofs.good();
}

std::optional<::Inode> KVStore::get(const std::string& key) {
    std::string path = key_to_path(key);
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return std::nullopt;
    std::streamsize size = ifs.tellg();
    ifs.seekg(0);
    if (size <= 0) return std::nullopt;
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(buf.data()), size);
    if (!ifs) return std::nullopt;

    size_t offset = 0;
    ::Inode out;
    if (!::Inode::deserialize(buf.data(), offset, out, buf.size())) return std::nullopt;
    return out;
}

bool KVStore::del(const std::string& key) {
    std::string path = key_to_path(key);
    std::error_code ec;
    return fs::remove(path, ec);
}

bool KVStore::put_raw(const std::string& key, const std::vector<uint8_t>& data) {
    std::string path = key_to_path(key);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    if (!data.empty()) {
        ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    return ofs.good();
}

std::optional<std::vector<uint8_t>> KVStore::get_raw(const std::string& key) {
    std::string path = key_to_path(key);
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return std::nullopt;
    std::streamsize size = ifs.tellg();
    ifs.seekg(0);
    if (size < 0) return std::nullopt;
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(buf.data()), size);
    if (!ifs) return std::nullopt;
    return buf;
}

bool KVStore::del_raw(const std::string& key) {
    return del(key);
}

#endif
