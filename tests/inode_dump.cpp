#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/mds/inode/InodeStorage.h"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string file;
    uint64_t count{10};
    uint64_t offset{0};
};

void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " --file <path> [--count N] [--offset K]\n"
              << "  --file    inode batch file path\n"
              << "  --count   number of inodes to print (default 10)\n"
              << "  --offset  start index (inode slot) (default 0)\n";
}

bool ParseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--file" && i + 1 < argc) {
            opts.file = argv[++i];
        } else if (arg == "--count" && i + 1 < argc) {
            opts.count = std::stoull(argv[++i]);
        } else if (arg == "--offset" && i + 1 < argc) {
            opts.offset = std::stoull(argv[++i]);
        } else if (arg == "--help") {
            PrintUsage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown arg: " << arg << "\n";
            return false;
        }
    }
    if (opts.file.empty()) {
        std::cerr << "--file is required\n";
        return false;
    }
    return true;
}

std::string NodeTypeName(uint8_t type) {
    switch (type & 0x03) {
        case 0: return "SSD";
        case 1: return "HDD";
        case 2: return "Mix";
        default: return "Reserved";
    }
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!ParseArgs(argc, argv, opts)) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::ifstream in(opts.file, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open file: " << opts.file << "\n";
        return 1;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff total_bytes = in.tellg();
    if (total_bytes <= 0) {
        std::cerr << "Empty file: " << opts.file << "\n";
        return 1;
    }
    in.seekg(0, std::ios::beg);

    const uint64_t slot_size = InodeStorage::INODE_DISK_SLOT_SIZE;
    const uint64_t total_slots = static_cast<uint64_t>(total_bytes) / slot_size;
    if (opts.offset >= total_slots) {
        std::cerr << "Offset out of range: " << opts.offset
                  << " (total slots " << total_slots << ")\n";
        return 1;
    }
    uint64_t to_read = opts.count;
    if (opts.offset + to_read > total_slots) {
        to_read = total_slots - opts.offset;
    }

    in.seekg(static_cast<std::streamoff>(opts.offset * slot_size), std::ios::beg);
    std::vector<uint8_t> slot(static_cast<size_t>(slot_size));
    for (uint64_t i = 0; i < to_read; ++i) {
        in.read(reinterpret_cast<char*>(slot.data()), static_cast<std::streamsize>(slot_size));
        if (in.gcount() != static_cast<std::streamsize>(slot_size)) {
            std::cerr << "Short read at index " << (opts.offset + i) << "\n";
            break;
        }
        size_t off = 0;
        Inode inode;
        if (!Inode::deserialize(slot.data(), off, inode, slot_size)) {
            std::cerr << "Deserialize failed at index " << (opts.offset + i) << "\n";
            continue;
        }
        std::cout << "inode[" << (opts.offset + i) << "]\n";
        std::cout << "  inode_id=" << inode.inode << "\n";
        std::cout << "  namespace_id=" << inode.getNamespaceId() << "\n";
        std::cout << "  node_id=" << inode.location_id.fields.node_id
                  << " node_type=" << NodeTypeName(inode.location_id.fields.node_type) << "\n";
        std::cout << "  file_size_bytes=" << inode.getFileSize() << "\n";
        std::cout << "  filename=" << inode.filename << "\n";
        std::cout << "  volume_id=" << inode.getVolumeUUID() << "\n";
        std::cout << "  block_segments=" << inode.block_segments.size() << "\n";
    }
    return 0;
}
