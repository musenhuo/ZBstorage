#include "metadataserver/MetadataManager.h"
#include "inode/inode.h"
#include <filesystem>
#include <iostream>
#include <cassert>

static void clean_path(const std::string& p) {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

int main() {
    const std::string base = "./_meta_ut_tmp";
    const std::string inode_path = base + "/inodes.bin";
    const std::string bitmap_path = base + "/bitmap.bin";
    const std::string kv_path = base + "/kv";

    clean_path(base);
    std::filesystem::create_directories(base);

    // 创建新的 MetadataManager，显式启用 KV 并指定 kv_path
    MetadataManager mm(inode_path, bitmap_path, /*create_new=*/true, /*start_inodeno=*/2, /*use_kv=*/true, kv_path);

    // 分配 inode 并创建 Inode 对象
    uint64_t ino = mm.allocate_inode(0644);
    assert(ino != static_cast<uint64_t>(-1));

    ::Inode inode;
    inode.inode = ino;
    inode.setFilename("/test/file.txt");
    inode.setFileType(static_cast<uint8_t>(FileType::Regular));
    inode.setFilePerm(0644);

    bool ok = mm.put_inode_for_path("/test/file.txt", inode);
    if (!ok) {
        std::cerr << "put_inode_for_path failed" << std::endl;
        return 2;
    }

    auto maybe = mm.get_inode_by_path("/test/file.txt");
    assert(maybe.has_value());
    auto got = *maybe;
    assert(got.inode == inode.inode);
    assert(got.filename == inode.filename);

    // 模拟重启：用相同文件路径重建 MetadataManager（create_new=false），并验证仍能通过 KV 查到
    MetadataManager mm2(inode_path, bitmap_path, /*create_new=*/false, /*start_inodeno=*/2, /*use_kv=*/true, kv_path);
    auto maybe2 = mm2.get_inode_by_path("/test/file.txt");
    assert(maybe2.has_value());
    auto got2 = *maybe2;
    assert(got2.inode == inode.inode);
    assert(got2.filename == inode.filename);

    std::cout << "[MetadataManager_test] PASS: parsed inode ino=" << got2.inode << " filename=" << got2.filename << std::endl;

    // cleanup
    clean_path(base);
    return 0;
}
