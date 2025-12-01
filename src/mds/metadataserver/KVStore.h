#pragma once
#include <string>
#include <optional>
#include <cstdint>

// Use the project's Inode definition
#include "../inode/inode.h"

namespace mds {

class KVStore {
public:
    explicit KVStore(const std::string& base_dir);
    // store the serialized bytes produced by ::Inode::serialize()
    bool put(const std::string& key, const ::Inode& value);
    std::optional<::Inode> get(const std::string& key);
    bool del(const std::string& key);
    // Raw byte operations (key and value may contain binary data)
    bool put_raw(const std::string& key, const std::vector<uint8_t>& data);
    std::optional<std::vector<uint8_t>> get_raw(const std::string& key);
    bool del_raw(const std::string& key);

private:
    std::string base_dir_;
    std::string key_to_path(const std::string& key) const;
};

} // namespace mds
