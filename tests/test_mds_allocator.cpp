#include <iostream>
#include <memory>
#include "../src/fs/VFS/VFS_new.h"
#include "../src/fs/volume/Volume.h"

int main() {
    // 初始化 FileSystem（创建新的元数据存储）
    FileSystem fs(true);

    // 创建并注册一个 SSD 卷
    auto vol = std::make_shared<Volume>("vol-uuid-1", "node-1", 10000);
    if (!fs.register_volume(vol, VolumeType::SSD)) {
        std::cerr << "register_volume failed" << std::endl;
        return 2;
    }

    // 创建文件，期望 MDS 在 CreateFile 后为 inode 分配 volume_id
    const std::string path = "/test_alloc_file";
    if (!fs.create_file(path, 0644)) {
        std::cerr << "create_file failed" << std::endl;
        return 3;
    }

    auto inode = fs.find_inode_by_path(path);
    if (!inode) {
        std::cerr << "find_inode_by_path failed" << std::endl;
        return 4;
    }

    std::cout << "inode: " << inode->inode << " volume_id: " << inode->getVolumeUUID() << std::endl;
    if (inode->getVolumeUUID().empty()) {
        std::cerr << "volume_id not set on inode" << std::endl;
        return 5;
    }

    std::cout << "Test passed: inode bound to volume" << std::endl;
    return 0;
}
