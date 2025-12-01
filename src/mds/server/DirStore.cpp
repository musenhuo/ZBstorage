#include "DirStore.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <system_error>
#include <unordered_map>

namespace {

// File-format constants --------------------------------------------------
// kDirMagic/kDirVersion: magic/version written at file head to detect incompatible
// formats. kOpInsert/kOpDelete denote log record opcodes.
constexpr uint32_t kDirMagic = 0x44525331; // "DRS1"
constexpr uint16_t kDirVersion = 1;
constexpr uint8_t kOpInsert = 1;
constexpr uint8_t kOpDelete = 2;

// DirectoryFileHeader
// 功能: 存放在每个目录文件开头的元数据，包含魔数/版本以及用于快速判断是否需要压缩的计数器。
// 字段:
//  - magic/version: 用于校验文件格式兼容性。
//  - reserved: 对齐/保留位。
//  - entry_count: 当前重放后内存中活跃的目录项数量。
//  - tombstone_count: 文件中记录的删除/墓碑数（用于触发压缩的启发式判断）。
struct DirectoryFileHeader {
    uint32_t magic{ kDirMagic };
    uint16_t version{ kDirVersion };
    uint16_t reserved{ 0 };
    uint32_t entry_count{ 0 };
    uint32_t tombstone_count{ 0 };
};

// 返回一个带有 magic/version 的默认头（所有计数器为 0）。
DirectoryFileHeader make_default_header() {
    return DirectoryFileHeader{};
}

// ensure_file_initialized
// 功能: 确保指定路径存在并已写入默认头（如果不存在则创建）。
// 参数: path - 目录文件路径（例如 base_dir_/dirs/<ino>.dir）。
// 返回: 文件已存在或创建成功返回 true；否则返回 false。
bool ensure_file_initialized(const std::string& path) {
    if (std::filesystem::exists(path)) {
        return true;
    }
    DirectoryFileHeader header = make_default_header();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.flush();
    return out.good();
}

// read_header
// 功能: 从输入流读取并验证 DirectoryFileHeader。
// 参数: in - 已打开的输入流；header - 输出参数，读取到的头信息。
// 返回: 成功读取并验证返回 true，否则 false（流不足或 magic/version 不匹配）。
bool read_header(std::istream& in, DirectoryFileHeader& header) {
    if (!in.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        return false;
    }
    return header.magic == kDirMagic && header.version == kDirVersion;
}

// write_header
// 功能: 将 header 写回到目录文件开头以持久化计数器更新。
// 参数: path - 目录文件路径；header - 要写入的头信息。
// 返回: 写入并 flush 成功返回 true，否则 false。
bool write_header(const std::string& path, const DirectoryFileHeader& header) {
    if (!ensure_file_initialized(path)) {
        return false;
    }
    std::fstream io(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!io.is_open()) {
        return false;
    }
    io.seekp(0, std::ios::beg);
    io.write(reinterpret_cast<const char*>(&header), sizeof(header));
    io.flush();
    return io.good();
}

// append_record
// 功能: 向目录文件尾追加一条操作记录（插入或删除）。
// 参数:
//  - path: 目录文件路径。
//  - opcode: 操作码（kOpInsert 或 kOpDelete）。
//  - type: 目录项类型（插入记录时使用，删除记录可忽略）。
//  - name: 目录项名称（字节序列）。
//  - inode: inode 编号（插入记录时使用，删除记录可为 0）。
// 返回: 成功写入并 flush 返回 true，否则 false。
bool append_record(const std::string& path,
                   uint8_t opcode,
                   FileType type,
                   const std::string& name,
                   uint64_t inode) {
    if (!ensure_file_initialized(path)) {
        return false;
    }
    std::fstream io(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!io.is_open()) {
        return false;
    }
    io.seekp(0, std::ios::end);
    uint8_t type_byte = static_cast<uint8_t>(type);
    uint16_t name_len = static_cast<uint16_t>(name.size());
    io.write(reinterpret_cast<const char*>(&opcode), sizeof(opcode));
    io.write(reinterpret_cast<const char*>(&type_byte), sizeof(type_byte));
    io.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
    if (name_len > 0) {
        io.write(name.data(), name_len);
    }
    io.write(reinterpret_cast<const char*>(&inode), sizeof(inode));
    io.flush();
    return io.good();
}

// should_compact
// 功能: 根据 header 中的 live/tombstone 计数判断是否需要压缩文件以回收空间。
// 参数: header - 当前统计值。
// 返回: 若 tombstone 数量超过阈值则返回 true（触发 compact），否则 false。
bool should_compact(const DirectoryFileHeader& header) {
    const uint32_t live = header.entry_count;
    const uint32_t tomb = header.tombstone_count;
    if (tomb == 0) return false;
    const uint32_t limit = std::max<uint32_t>(32, live * 2);
    return tomb > limit;
}

// compact_file
// 功能: 将 live entries 按给定顺序写回到文件（truncate + 重写），并生成新的 header。
// 参数:
//  - path: 目标目录文件路径。
//  - header: 输出参数，写入后更新为 fresh header（entry_count/tombstone_count 已更新）。
//  - order: 键的稳定顺序，用于保证重写后目录项顺序一致（可用于可预测的迭代顺序）。
//  - entries: 名称->DirectoryEntry 的映射，包含所有 live 条目。
// 返回: 写入并 flush 成功返回 true，否则 false。
bool compact_file(const std::string& path,
                  DirectoryFileHeader& header,
                  const std::vector<std::string>& order,
                  const std::unordered_map<std::string, DirectoryEntry>& entries) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    DirectoryFileHeader fresh = make_default_header();
    fresh.entry_count = static_cast<uint32_t>(entries.size());
    fresh.tombstone_count = 0;
    out.write(reinterpret_cast<const char*>(&fresh), sizeof(fresh));
    for (const auto& name : order) {
        auto it = entries.find(name);
        if (it == entries.end()) {
            continue;
        }
        const DirectoryEntry& entry = it->second;
        std::string entry_name(entry.name, entry.name_len);
        uint8_t opcode = kOpInsert;
        uint8_t type = static_cast<uint8_t>(entry.file_type);
        uint16_t name_len = static_cast<uint16_t>(entry_name.size());
        uint64_t ino = entry.inode;
        out.write(reinterpret_cast<const char*>(&opcode), sizeof(opcode));
        out.write(reinterpret_cast<const char*>(&type), sizeof(type));
        out.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
        if (name_len > 0) {
            out.write(entry_name.data(), name_len);
        }
        out.write(reinterpret_cast<const char*>(&ino), sizeof(ino));
    }
    out.flush();
    if (!out.good()) {
        return false;
    }
    header = fresh;
    return true;
}

// load_state
// 功能: 读取并回放目录文件头与后续日志记录，重建 entries 和 order。
// 参数:
//  - path: 源目录文件路径。
//  - header: 输出参数，读取到的头信息（若文件不存在则为默认头）。
//  - order: 输出参数，按首次插入顺序记录名称（便于重写时保持顺序）。
//  - entries: 输出参数，名称->DirectoryEntry 映射，包含回放后的 live 条目。
// 返回: 成功返回 true（文件不存在也视作空目录并返回 true）；失败仅在 I/O 出错时返回 false。
bool load_state(const std::string& path,
                DirectoryFileHeader& header,
                std::vector<std::string>& order,
                std::unordered_map<std::string, DirectoryEntry>& entries) {
    order.clear();
    entries.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        header = make_default_header();
        return true;
    }
    if (!read_header(in, header)) {
        in.close();
        ensure_file_initialized(path);
        header = make_default_header();
        return true;
    }
    uint32_t tombstones = 0;
    while (true) {
        uint8_t opcode = 0;
        if (!in.read(reinterpret_cast<char*>(&opcode), sizeof(opcode))) {
            break;
        }
        uint8_t type = 0;
        uint16_t name_len = 0;
        uint64_t ino = 0;
        if (!in.read(reinterpret_cast<char*>(&type), sizeof(type))) break;
        if (!in.read(reinterpret_cast<char*>(&name_len), sizeof(name_len))) break;
        std::string name(name_len, '\0');
        if (name_len > 0 && !in.read(name.data(), name_len)) break;
        if (!in.read(reinterpret_cast<char*>(&ino), sizeof(ino))) break;

        if (opcode == kOpInsert) {
            DirectoryEntry entry(name, ino, static_cast<FileType>(type));
            auto [it, inserted] = entries.emplace(name, entry);
            if (inserted) {
                order.push_back(name);
            } else {
                it->second = entry;
            }
        } else if (opcode == kOpDelete) {
            tombstones++;
            entries.erase(name);
        }
    }
    header.entry_count = static_cast<uint32_t>(entries.size());
    header.tombstone_count = tombstones;
    return true;
}

// make_entry_name
// 功能: 从 DirectoryEntry 结构中构造 std::string（注意 entry.name 不是以\0 结尾）。
// 参数: entry - 源目录项。
// 返回: 名称字符串。
std::string make_entry_name(const DirectoryEntry& entry) {
    return std::string(entry.name, entry.name_len);
}

} // namespace

std::string DirStore::dir_file_path(uint64_t dir_ino) const {
    return base_dir_ + "/dirs/" + std::to_string(dir_ino) + ".dir";
}

bool DirStore::ensure_dir() const {
    std::error_code ec;
    std::filesystem::create_directories(base_dir_ + "/dirs", ec);
    return !ec;
}

bool DirStore::read(uint64_t dir_ino, std::vector<DirectoryEntry>& out) {
    out.clear();
    if (!ensure_dir()) return false;

    auto path = dir_file_path(dir_ino);
    DirectoryFileHeader header;
    std::vector<std::string> order;
    std::unordered_map<std::string, DirectoryEntry> entries;
    if (!load_state(path, header, order, entries)) return false;

    for (const auto& name : order) {
        auto it = entries.find(name);
        if (it != entries.end()) {
            out.push_back(it->second);
        }
    }

    if (should_compact(header)) {
        if (!compact_file(path, header, order, entries)) {
            return false;
        }
    }
    return true;
}

bool DirStore::add(uint64_t dir_ino, const DirectoryEntry& entry) {
    if (!ensure_dir()) return false;

    auto path = dir_file_path(dir_ino);
    DirectoryFileHeader header;
    std::vector<std::string> order;
    std::unordered_map<std::string, DirectoryEntry> entries;
    if (!load_state(path, header, order, entries)) return false;

    std::string name = make_entry_name(entry);
    if (entries.count(name)) {
        return false; // duplicate
    }

    if (!append_record(path, kOpInsert, entry.file_type, name, entry.inode)) {
        return false;
    }

    entries.emplace(name, entry);
    order.push_back(name);
    header.entry_count = static_cast<uint32_t>(entries.size());

    if (!write_header(path, header)) {
        return false;
    }

    if (should_compact(header)) {
        return compact_file(path, header, order, entries);
    }
    return true;
}

bool DirStore::remove(uint64_t dir_ino, const std::string& name) {
    if (!ensure_dir()) return false;

    auto path = dir_file_path(dir_ino);
    DirectoryFileHeader header;
    std::vector<std::string> order;
    std::unordered_map<std::string, DirectoryEntry> entries;
    if (!load_state(path, header, order, entries)) return false;

    auto it = entries.find(name);
    if (it == entries.end()) {
        return false;
    }

    entries.erase(it);
    header.entry_count = static_cast<uint32_t>(entries.size());
    header.tombstone_count += 1;

    if (!append_record(path, kOpDelete, FileType::Unknown, name, 0)) {
        return false;
    }

    if (!write_header(path, header)) {
        return false;
    }

    if (should_compact(header)) {
        return compact_file(path, header, order, entries);
    }
    return true;
}

bool DirStore::reset(uint64_t dir_ino) {
    if (!ensure_dir()) return false;

    auto path = dir_file_path(dir_ino);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
        return false;
    }
    return true;
}