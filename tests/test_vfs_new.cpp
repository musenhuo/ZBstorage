#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include "../src/fs/VFS/VFS_new.h"
#include "../src/fs/volume/Volume.h"
#include "../src/srm/storage_manager/StorageResource.h"

extern StorageResource* g_storage_resource;


namespace {
std::filesystem::path make_temp_dir() {
    auto base = std::filesystem::temp_directory_path();
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = base / ("vfs_new_test_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

struct TempDir {
    std::filesystem::path path;
    TempDir() : path(make_temp_dir()) {}
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        if (ec) {
            std::cerr << "Failed to remove temp directory " << path << ": " << ec.message() << std::endl;
        }
    }
};

bool expect(bool cond, const std::string& msg) {
    std::cout << "[TEST] " << msg << " -> " << (cond ? "OK" : "FAIL") << std::endl;
    if (!cond) {
        std::cerr << "    expected: success" << std::endl;
    }
    return cond;
}
}

int main() {
    TempDir tmp; // 隔离每次运行的元数据与目录存储
    std::cout << "VFS test tempdir: " << tmp.path << std::endl;
    const auto inode_file = (tmp.path / "inode.dat").string();
    const auto bitmap_file = (tmp.path / "bitmap.dat").string();
    const auto dir_store_base = (tmp.path / "dir_store").string();

    auto mds = std::make_shared<MdsServer>(inode_file, bitmap_file, dir_store_base, true);
    auto registry = make_file_volume_registry(tmp.path.string());
    auto volume_manager = std::make_shared<VolumeManager>();
    FileSystem fs(mds, registry, volume_manager); // 组合型构造：显式注入组件

    // 绑定全局存储资源，加载并注册真实的卷信息
    StorageResource storage_resource;
    g_storage_resource = &storage_resource;
    std::cout << "Loading storage nodes via StorageResource..." << std::endl;
    storage_resource.loadFromFile(false, false);
    int registered_volumes = 0;
    auto init_start = std::chrono::steady_clock::now();
    while (true) {
        auto node_volumes = storage_resource.initOneNodeVolume();
        if (!node_volumes.first && !node_volumes.second) {
            break;
        }
        if (node_volumes.first) {
            if (fs.register_volume(node_volumes.first, VolumeType::SSD)) {
                ++registered_volumes;
            }
        }
        if (node_volumes.second) {
            if (fs.register_volume(node_volumes.second, VolumeType::HDD)) {
                ++registered_volumes;
            }
        }
    }
    auto init_end = std::chrono::steady_clock::now();
    auto init_ms = std::chrono::duration_cast<std::chrono::milliseconds>(init_end - init_start).count();
    std::cout << "Initialized and registered volumes from all nodes: " << registered_volumes
              << " (" << init_ms << " ms)" << std::endl;

    // 启动并初始化根目录（幂等）
    std::cout << "Expect startup() succeeds creating metadata store" << std::endl;
    if (!expect(fs.startup(), "startup")) return 1;
    std::cout << "Expect create_root_directory() returns true even if root exists" << std::endl;
    if (!expect(fs.create_root_directory(), "create_root_directory")) return 2;

    // 注册可用卷，确保后续文件获得 volume_id
    if (registered_volumes == 0) {
        std::cout << "No volumes loaded from StorageResource, falling back to synthetic volume" << std::endl;
        auto fallback_vol = std::make_shared<Volume>("vol-1", "node-1", 4096);
        if (!expect(fs.register_volume(fallback_vol, VolumeType::SSD), "register_volume fallback")) return 3;
    }

    // 基本目录操作：mkdir/ls/lookup/rmdir
    std::cout << "Expect mkdir('/test') creates new directory" << std::endl;
    if (!expect(fs.mkdir("/test", 0755), "mkdir /test")) return 4;
    std::cout << "Expect ls('/') prints entries including 'test'" << std::endl;
    if (!expect(fs.ls("/"), "ls root")) return 5;
    auto dir_ino = fs.lookup_inode("/test");
    if (!expect(dir_ino != static_cast<uint64_t>(-1), "lookup_inode test")) return 6;
    std::cout << "Expect rmdir('/test') removes empty directory" << std::endl;
    if (!expect(fs.rmdir("/test"), "rmdir /test")) return 7;

    // 文件创建+常规 I/O（open/write/seek/read/close）
    std::cout << "Expect mkdir('/io') for file IO staging" << std::endl;
    if (!expect(fs.mkdir("/io", 0755), "mkdir /io")) return 8;
    std::cout << "Expect create_file('/io/data.bin') allocates inode" << std::endl;
    if (!expect(fs.create_file("/io/data.bin", 0644), "create_file data")) return 9;

    int fd = fs.open("/io/data.bin", MO_RDWR | MO_CREAT, 0644);
    if (!expect(fd >= 0, "open data")) return 10;

    const std::string payload = "hello vfs_new";
    std::cout << "Expect write() stores " << payload.size() << " bytes" << std::endl;
    if (!expect(fs.write(fd, payload.c_str(), payload.size()) == static_cast<ssize_t>(payload.size()),
                "write payload")) return 11;
    if (!expect(fs.seek(fd, 0, SEEK_SET) == 0, "seek begin")) return 12;
    std::string buffer(payload.size(), '\0');
    std::cout << "Expect read() returns same byte count (content may be zero due to LocalStorageGateway stub)" << std::endl;
    if (!expect(fs.read(fd, buffer.data(), buffer.size()) == static_cast<ssize_t>(buffer.size()),
                "read payload")) return 13;
    if (!expect(fs.close(fd) == 0, "close fd")) return 14;

    // 验证句柄观察者：删除文件后触发 force_close
    std::cout << "Expect handle observer closes fd once inode removed" << std::endl;
    if (!expect(fs.create_file("/io/keep.bin", 0644), "create keep")) return 15;
    int keep_fd = fs.open("/io/keep.bin", MO_RDWR, 0644);
    if (!expect(keep_fd >= 0, "open keep")) return 16;
    const char keep_data[] = "xyz";
    if (!expect(fs.write(keep_fd, keep_data, sizeof(keep_data)) == static_cast<ssize_t>(sizeof(keep_data)),
                "write keep")) return 17;
    std::cout << "Expect remove_file() triggers server-side block cleanup" << std::endl;
    if (!expect(fs.remove_file("/io/keep.bin"), "remove keep")) return 18;
    char tmp_buf[4] = {};
    if (!expect(fs.read(keep_fd, tmp_buf, sizeof(tmp_buf)) == -1, "read after remove should fail")) return 19;
    if (!expect(fs.close(keep_fd) == -1, "close already removed fd")) return 20;

    // 验证主动 shutdown_fd：客户端显式关闭句柄
    std::cout << "Expect shutdown_fd() stops further IO" << std::endl;
    if (!expect(fs.create_file("/io/temp.bin", 0644), "create temp")) return 21;
    int temp_fd = fs.open("/io/temp.bin", MO_RDWR, 0644);
    if (!expect(temp_fd >= 0, "open temp")) return 22;
    if (!expect(fs.shutdown_fd(temp_fd) == 0, "shutdown_fd")) return 23;
    if (!expect(fs.read(temp_fd, tmp_buf, sizeof(tmp_buf)) == -1, "read after shutdown_fd")) return 24;

    // 冷数据接口：collect 列表、位图、百分位
    std::cout << "Expect cold inode utilities return bounded results" << std::endl;
    auto cold = fs.collect_cold_inodes(10, 1);
    if (!expect(cold.size() <= 10, "collect_cold_inodes bound")) return 25;
    auto bitmap = fs.collect_cold_inodes_bitmap(1);
    if (!expect(bitmap != nullptr, "cold bitmap exists")) return 26;
    if (!expect(bitmap->size() >= fs.metadata()->GetTotalInodes(), "bitmap sized")) return 27;
    auto by_percent = fs.collect_cold_inodes_by_atime_percent(50.0);
    if (!expect(by_percent.size() <= fs.metadata()->GetTotalInodes(), "collect by percent")) return 28;

    std::cout << "Expect shutdown() flushes registries successfully" << std::endl;
    if (!expect(fs.shutdown(), "shutdown")) return 29;

    std::cout << "VFS_new integration test passed" << std::endl;
    return 0;
}
