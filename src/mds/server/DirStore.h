#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include "../inode/inode.h"
#include "../namespace/Directory.h"

class DirStore {
private:
    std::string base_dir_;

public:
    explicit DirStore(std::string base_dir)
        : base_dir_(std::move(base_dir)) {}

    // 读取目录项
    bool read(uint64_t dir_ino, std::vector<DirectoryEntry>& out);

    // 追加目录项（若重名返回 false）
    bool add(uint64_t dir_ino, const DirectoryEntry& entry);

    // 按名字删除目录项（返回是否删除成功）
    bool remove(uint64_t dir_ino, const std::string& name);

    // 删除整个目录文件（目录被删除或 inode 回收时调用）
    bool reset(uint64_t dir_ino);

private:
    std::string dir_file_path(uint64_t dir_ino) const;
    bool ensure_dir() const;
};