#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

enum class IOType {
    Read,
    Write,
    Delete,
};

// 在文件系统向元数据服务器获取文件的inode后，会向存储端发送IO请求，包括读、写、创建、和删除操作
// IO请求包括起始块号和块长度（可能只有一个块）
// 存储端目前主要是根据IO序列返回一个性能模型（比如先写多少文件，再读多少文件，以及顺序和随机）

class IORequest {
public:
    IOType type;
    std::string storage_node_id;   // 旧字段
    std::string node_id;           // 新别名
    std::string volume_id;         // 旧字段
    std::string volume_uuid;       // 新别名
    size_t start_block;
    size_t block_count;
    size_t offset_in_block;        // 旧字段
    size_t offset;                 // 新别名
    size_t data_size;              // 旧字段
    size_t length;                 // 新别名
    void* buffer;
    size_t buffer_size;

    IORequest(IOType t,
              std::string node,
              const std::string& uuid,
              size_t start,
              size_t count = 1,
              size_t off = 0,
              size_t size = 0,
              void* buf = nullptr,
              size_t buf_size = 0)
        : type(t),
          storage_node_id(std::move(node)),
          node_id(storage_node_id),
          volume_id(uuid),
          volume_uuid(volume_id),
          start_block(start),
          block_count(count),
          offset_in_block(off),
          offset(off),
          data_size(size),
          length(size),
          buffer(buf),
          buffer_size(buf_size) {}

    IORequest()
        : type(IOType::Read),
          storage_node_id(),
          node_id(),
          volume_id(),
          volume_uuid(),
          start_block(0),
          block_count(0),
          offset_in_block(0),
          offset(0),
          data_size(0),
          length(0),
          buffer(nullptr),
          buffer_size(0) {}

    void sync_aliases() {
        node_id = storage_node_id;
        volume_uuid = volume_id;
        offset = offset_in_block;
        length = data_size;
    }
};

