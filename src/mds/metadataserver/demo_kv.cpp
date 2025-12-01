#include "KVStore.h"
#include "MetadataManager.h"
#include <iostream>

int main() {
    // create MetadataManager with KV enabled and path /tmp/zbstorage_kv
    MetadataManager mgr("/tmp/unused_inode_storage.bin", "/tmp/unused_bitmap.bin", true, 2, true, "/tmp/zbstorage_kv");

    std::string path = "/home/user/docs/report.txt";

    // allocate an inode and then write the full inode under path key
    uint64_t ino = mgr.allocate_inode(0);
    std::cout << "allocated ino=" << ino << "\n";

    ::Inode inode;
    inode.inode = ino;
    inode.setFileSize(4096);
    inode.setSizeUnit(0);
    inode.setFilename(std::string("report.txt"));

    if (!mgr.put_inode_for_path(path, inode)) {
        std::cerr << "put_inode_for_path failed\n";
        return 1;
    }
    std::cout << "put_inode_for_path ok\n";

    auto got = mgr.get_inode_by_path(path);
    if (!got) {
        std::cerr << "get_inode_by_path failed\n";
        return 2;
    }
    std::cout << "got inode by path: ino=" << got->inode << " filename=" << got->filename << " size=" << got->getFileSize() << "\n";

    if (mgr.delete_inode_path(path)) {
        std::cout << "delete_inode_path ok\n";
    } else {
        std::cout << "delete_inode_path failed\n";
    }

    return 0;
}
