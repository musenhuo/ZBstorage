#include "metadataserver/MetadataManager.h"
#include "inode/inode.h"
#include <filesystem>
#include <iostream>
#include <cassert>
#include <chrono>

static void clean_path(const std::string& p) {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

int main() {
    const std::string base = "./_meta_bulk_ut_tmp";
    const std::string inode_path = base + "/inodes.bin";
    const std::string bitmap_path = base + "/bitmap.bin";
    const std::string kv_path = base + "/kv";

    clean_path(base);
    std::filesystem::create_directories(base);

    // Parameters
    constexpr int kNumEntries = 2000; // 批量数量，可根据需要调整

    // 创建 MetadataManager 并启用 KV
    MetadataManager mm(inode_path, bitmap_path, /*create_new=*/true, /*start_inodeno=*/2, /*use_kv=*/true, kv_path);

    std::cout << "[bulk_test] inserting " << kNumEntries << " entries..." << std::endl;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kNumEntries; ++i) {
        std::string path = "/bulk/f" + std::to_string(i);
        uint64_t ino = mm.allocate_inode(0644);
        if (ino == static_cast<uint64_t>(-1)) {
            std::cerr << "allocate_inode failed at " << i << std::endl;
            return 2;
        }
        ::Inode inode;
        inode.inode = ino;
        inode.setFilename(path);
        inode.setFileType(static_cast<uint8_t>(FileType::Regular));
        inode.setFilePerm(0644);
        if (!mm.put_inode_for_path(path, inode)) {
            std::cerr << "put_inode_for_path failed at " << i << std::endl;
            return 3;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    std::chrono::duration<double> insert_d = t1 - t0;
    std::cout << "[bulk_test] inserted " << kNumEntries << " entries in " << insert_d.count() << "s" << std::endl;

    std::cout << "[bulk_test] verifying reads..." << std::endl;
    auto r0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kNumEntries; ++i) {
        std::string path = "/bulk/f" + std::to_string(i);
        auto maybe = mm.get_inode_by_path(path);
        if (!maybe.has_value()) {
            std::cerr << "get_inode_by_path missing for " << path << std::endl;
            return 4;
        }
        auto got = *maybe;
        if (got.filename != path) {
            std::cerr << "mismatch filename for " << path << " got=" << got.filename << std::endl;
            return 5;
        }
    }
    auto r1 = std::chrono::steady_clock::now();
    std::chrono::duration<double> read_d = r1 - r0;
    std::cout << "[bulk_test] verified " << kNumEntries << " reads in " << read_d.count() << "s" << std::endl;

    // 模拟重启并再次验证
    std::cout << "[bulk_test] simulating restart..." << std::endl;
    MetadataManager mm2(inode_path, bitmap_path, /*create_new=*/false, /*start_inodeno=*/2, /*use_kv=*/true, kv_path);
    auto s0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kNumEntries; ++i) {
        std::string path = "/bulk/f" + std::to_string(i);
        auto maybe = mm2.get_inode_by_path(path);
        if (!maybe.has_value()) {
            std::cerr << "post-restart missing for " << path << std::endl;
            return 6;
        }
    }
    auto s1 = std::chrono::steady_clock::now();
    std::chrono::duration<double> restart_read_d = s1 - s0;
    std::cout << "[bulk_test] post-restart verified " << kNumEntries << " reads in " << restart_read_d.count() << "s" << std::endl;

    std::cout << "[bulk_test] PASS" << std::endl;
    clean_path(base);
    return 0;
}
