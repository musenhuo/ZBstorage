#include "server/Server.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

static void clean_path(const std::string& p) {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

int main() {
    const std::string base = "./_mds_ut_tmp";
    const std::string inode_path = base + "/inodes.bin";
    const std::string bitmap_path = base + "/bitmap.bin";
    const std::string dirstore_path = base + "/dir";

    clean_path(base);
    std::filesystem::create_directories(base);

    MdsServer mds(inode_path, bitmap_path, dirstore_path, /*create_new=*/true);

    // 根目录
    /*
        CreateRoot 调用两次，验证幂等性（第二次仍返回 true）。
    */
    assert(mds.CreateRoot());
    assert(mds.CreateRoot()); // 幂等

    // 目录与文件
    /*
        Mkdir("/a") 与 Mkdir("/a/b") 成功。
        CreateFile("/a/b/f1") 成功。
        Ls("/a/b") 成功（且会打印目录内容）。
    */
    assert(mds.Mkdir("/a", 0755));
    assert(mds.Mkdir("/a/b", 0755));
    assert(mds.CreateFile("/a/b/f1", 0644));
    auto f1_ino = mds.LookupIno("/a/b/f1");
    assert(f1_ino != static_cast<uint64_t>(-1));
    assert(mds.Ls("/a/b"));

    // 非空目录删除失败
    /*
        Rmdir("/a/b") 在目录非空时应返回 false（assert 期望失败返回）。
    */
    assert(!mds.Rmdir("/a/b"));
    // 删文件后再删目录
    /*
        RemoveFile("/a/b/f1") 成功。
        新建 /a/b/f2，应复用 f1 的 inode。
        移除 f2 后，Rmdir("/a/b") 成功。    
    */
    assert(mds.RemoveFile("/a/b/f1"));
    assert(mds.CreateFile("/a/b/f2", 0644));
    auto f2_ino = mds.LookupIno("/a/b/f2");
    assert(f2_ino == f1_ino);
    assert(mds.RemoveFile("/a/b/f2"));
    assert(mds.Rmdir("/a/b"));

    // 路径解析
    /*
        LookupIno("/a") 返回有效 inode。
        FindInodeByPath("/a") 读回的 inode 与 LookupIno 一致。
    */
    auto ino_a = mds.LookupIno("/a");
    assert(ino_a != static_cast<uint64_t>(-1));
    auto inode_a = mds.FindInodeByPath("/a");
    assert(inode_a && inode_a->inode == ino_a);

    // 重启后重建命名空间
    /*
        使用 create_new=false 重建 MdsServer，调用 RebuildInodeTable 后再次验证 "/a" 的 inode 可查且一致。
    */
    {
        MdsServer mds2(inode_path, bitmap_path, dirstore_path, /*create_new=*/false);
        mds2.RebuildInodeTable();
        auto ino_a2 = mds2.LookupIno("/a");
        assert(ino_a2 == ino_a);
        auto inode_a2 = mds2.FindInodeByPath("/a");
        assert(inode_a2 && inode_a2->inode == ino_a);
    }

    // 冷扫描
    auto cold = mds.CollectColdInodes(/*max=*/10, /*min_age_windows=*/1);
    (void)cold;

    // 批量目录项：验证增量追加与压缩
    assert(mds.Mkdir("/bulk", 0755));
    std::vector<std::string> bulk_files;
    constexpr int kBulkFiles = 200;
    constexpr int kKeepEntries = 20;
    bulk_files.reserve(kBulkFiles);
    for (int i = 0; i < kBulkFiles; ++i) {
        std::string path = "/bulk/f" + std::to_string(i);
        assert(mds.CreateFile(path, 0644));
        bulk_files.push_back(path);
    }
    auto bulk_ino = mds.LookupIno("/bulk");
    assert(bulk_ino != static_cast<uint64_t>(-1));
    auto bulk_dir_file = std::filesystem::path(dirstore_path) / "dirs" /
        (std::to_string(bulk_ino) + ".dir");
    auto size_before = std::filesystem::exists(bulk_dir_file)
        ? std::filesystem::file_size(bulk_dir_file)
        : 0;

    int removed = 0;
    for (int i = 0; i < kBulkFiles - kKeepEntries; ++i) {
        assert(mds.RemoveFile(bulk_files[i]));
        ++removed;
    }
    assert(removed > 0);
    // 触发读取以驱动压缩
    assert(mds.Ls("/bulk"));

    auto size_after = std::filesystem::exists(bulk_dir_file)
        ? std::filesystem::file_size(bulk_dir_file)
        : 0;
    assert(size_after < size_before);

    for (int i = kBulkFiles - kKeepEntries; i < kBulkFiles; ++i) {
        assert(mds.RemoveFile(bulk_files[i]));
    }
    assert(mds.Rmdir("/bulk"));

    std::cout << "[MDS UT] all tests passed." << std::endl;
    clean_path(base);
    return 0;
}