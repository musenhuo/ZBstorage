#include "dfs_client.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <errno.h>    // 关键：用于设置 errno
#include <stdarg.h>   // 关键：用于处理 dfs_open 的可变参数
#include <memory>
#include <string>
#include <vector>
#include <fcntl.h>    // [已修复] O_CREAT 和
#include <string.h>   // [已修复] memset, strncpy 所需的头文件

// 包含 proto 生成的文件
#include "posix.pb.h"

//
// ========================= 关键修改点 =========================
//
// --- readdir 的 C++ 内部实现 ---
// DfsDirStream 的 C++ 定义
// 我们把它从匿名命名空间移到了这里（顶层）
// 这样 extern "C" 函数才能看到它的完整定义
struct DfsDirStream {
    std::vector<posix::DirEntry> entries; // 存储从 RPC 获取的所有条目
    size_t current_index;                 // 当前读到的索引
    struct dirent current_entry;         // 可复用的 dirent 结构体
};
// =============================================================
//


// 使用匿名命名空间来隐藏实现细节
namespace {

/**
 * @brief bRPC 客户端的 C++ 封装
 * 我们使用单例模式，确保整个应用程序共享一个 Channel 和 Stub
 */
class BrpcClientImpl {
public:
    // ... (getInstance, init, is_initialized, getStub 等函数保持不变) ...
    // 获取单例实例
    static BrpcClientImpl& getInstance() {
        // C++11 保证了 static 局部变量的线程安全初始化
        static BrpcClientImpl instance;
        return instance;
    }

    // 初始化
    void init(const std::string& server_address) {
        if (m_stub) return; // 防止重复初始化

        m_channel = std::make_unique<brpc::Channel>();
        brpc::ChannelOptions options;
        options.protocol = "baidu_std";
        options.timeout_ms = 2000;
        options.max_retry = 3;

        if (m_channel->Init(server_address.c_str(), &options) != 0) {
            LOG(ERROR) << "Failed to initialize channel to " << server_address;
            m_stub = nullptr;
        } else {
            m_stub = std::make_unique<posix::PosixService_Stub>(m_channel.get());
        }
    }

    // 检查是否初始化成功
    bool is_initialized() const {
        return m_stub != nullptr;
    }

    // 获取 bRPC 存根
    posix::PosixService_Stub* getStub() {
        return m_stub.get();
    }

private:
    // 构造函数私有化
    BrpcClientImpl() = default;
    // 拷贝和赋值私有化
    BrpcClientImpl(const BrpcClientImpl&) = delete;
    BrpcClientImpl& operator=(const BrpcClientImpl&) = delete;

    std::unique_ptr<brpc::Channel> m_channel;
    std::unique_ptr<posix::PosixService_Stub> m_stub;
};

// ... (check_init_and_stub 和 set_errno_from_rpc 函数保持不变) ...

inline bool check_init_and_stub() {
    if (!BrpcClientImpl::getInstance().is_initialized()) {
        errno = ENOSYS; // "Function not implemented" 是一个合适的错误码
        return false;
    }
    return true;
}

inline void set_errno_from_rpc(brpc::Controller& cntl, int proto_error) {
    if (cntl.Failed()) {
        errno = ECOMM;
    } else {
        errno = proto_error;
    }
}


// --- DfsDirStream 的定义已从此位置移除 ---

} // 匿名命名空间结束


// --- C 风格 API 的实现 ---

extern "C" {

// ... (dfs_init, dfs_stat, dfs_open, dfs_read, dfs_write, dfs_close 保持不变) ...

void dfs_init(const char* server_address) {
    BrpcClientImpl::getInstance().init(server_address);
}

int dfs_stat(const char* path, struct stat* buf) {
    if (!check_init_and_stub()) return -1;

    posix::StatRequest req;
    posix::StatResponse res;
    brpc::Controller cntl;
    req.set_path(path);

    BrpcClientImpl::getInstance().getStub()->Stat(&cntl, &req, &res, nullptr);

    if (cntl.Failed() || res.error() != 0) {
        set_errno_from_rpc(cntl, res.error());
        return -1;
    }

    memset(buf, 0, sizeof(struct stat));
    const auto& info = res.stat_info();
    buf->st_mode = info.mode();
    buf->st_size = info.size();
    buf->st_atime = info.atime();
    buf->st_mtime = info.mtime();
    buf->st_ctime = info.ctime();
    buf->st_nlink = info.nlink();
    buf->st_uid = info.uid();
    buf->st_gid = info.gid();
    
    return 0;
}

int dfs_open(const char* path, int flags, ...) {
    if (!check_init_and_stub()) return -1;

    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }

    posix::OpenRequest req;
    posix::OpenResponse res;
    brpc::Controller cntl;
    req.set_path(path);
    req.set_flags(flags);
    req.set_mode(mode);

    BrpcClientImpl::getInstance().getStub()->Open(&cntl, &req, &res, nullptr);

    if (cntl.Failed() || res.error() != 0) {
        set_errno_from_rpc(cntl, res.error());
        return -1;
    }

    return res.fd();
}

ssize_t dfs_read(int fd, void* buf, size_t count) {
    if (!check_init_and_stub()) return -1;

    posix::ReadRequest req;
    posix::ReadResponse res;
    brpc::Controller cntl;
    req.set_fd(fd);
    req.set_count(count);

    BrpcClientImpl::getInstance().getStub()->Read(&cntl, &req, &res, nullptr);

    if (cntl.Failed() || res.error() != 0) {
        set_errno_from_rpc(cntl, res.error());
        return -1;
    }

    memcpy(buf, res.data().data(), res.bytes_read());
    return res.bytes_read();
}

ssize_t dfs_write(int fd, const void* buf, size_t count) {
    if (!check_init_and_stub()) return -1;

    posix::WriteRequest req;
    posix::WriteResponse res;
    brpc::Controller cntl;
    req.set_fd(fd);
    req.set_data(buf, count);

    BrpcClientImpl::getInstance().getStub()->Write(&cntl, &req, &res, nullptr);

    if (cntl.Failed() || res.error() != 0) {
        set_errno_from_rpc(cntl, res.error());
        return -1;
    }

    return res.bytes_written();
}

ssize_t dfs_pread(int fd, void* buf, size_t count, off_t offset) {
    if (!check_init_and_stub()) return -1;

    posix::PReadRequest req; // [修改]
    posix::PReadResponse res; // [修改]
    brpc::Controller cntl;
    req.set_fd(fd);
    req.set_count(count);
    req.set_offset(offset); // [新增] 设置偏移量

    BrpcClientImpl::getInstance().getStub()->PRead(&cntl, &req, &res, nullptr); // [修改]

    if (cntl.Failed() || res.error() != 0) {
        set_errno_from_rpc(cntl, res.error());
        return -1;
    }

    memcpy(buf, res.data().data(), res.bytes_read());
    return res.bytes_read();
}

ssize_t dfs_pwrite(int fd, const void* buf, size_t count, off_t offset) {
    if (!check_init_and_stub()) return -1;

    posix::PWriteRequest req; // [修改]
    posix::PWriteResponse res; // [修改]
    brpc::Controller cntl;
    req.set_fd(fd);
    req.set_data(buf, count);
    req.set_offset(offset); // [新增] 设置偏移量

    BrpcClientImpl::getInstance().getStub()->PWrite(&cntl, &req, &res, nullptr); // [修改]

    if (cntl.Failed() || res.error() != 0) {
        set_errno_from_rpc(cntl, res.error());
        return -1;
    }

    return res.bytes_written();
}

int dfs_close(int fd) {
    if (!check_init_and_stub()) return -1;

    posix::CloseRequest req;
    posix::CloseResponse res;
    brpc::Controller cntl;
    req.set_fd(fd);

    BrpcClientImpl::getInstance().getStub()->Close(&cntl, &req, &res, nullptr);

    if (cntl.Failed() || res.error() != 0) {
        set_errno_from_rpc(cntl, res.error());
        return -1;
    }

    return 0;
}


DfsDir* dfs_opendir(const char* path) {
    if (!check_init_and_stub()) return nullptr;

    posix::ReadDirRequest req;
    posix::ReadDirResponse res;
    brpc::Controller cntl;
    req.set_path(path);

    BrpcClientImpl::getInstance().getStub()->ReadDir(&cntl, &req, &res, nullptr);

    if (cntl.Failed() || res.error() != 0) {
        set_errno_from_rpc(cntl, res.error());
        return nullptr;
    }

    // 成功，创建一个 DfsDirStream 实例
    // 现在编译器能看到 DfsDirStream 的完整定义，这一行可以成功执行了
    DfsDir* dirp = new (std::nothrow) DfsDirStream();
    if (!dirp) {
        errno = ENOMEM;
        return nullptr;
    }

    dirp->current_index = 0;
    // 这一行也可以成功执行了
    for (const auto& entry : res.entries()) {
        dirp->entries.push_back(entry);
    }
    return dirp;
}

struct dirent* dfs_readdir(DfsDir* dirp) {
    if (!dirp) {
        errno = EBADF;
        return nullptr;
    }

    // 这一行也可以成功执行了
    if (dirp->current_index >= dirp->entries.size()) {
        return nullptr; // 到达目录末尾
    }

    // 这一行也可以成功执行了
    const auto& entry = dirp->entries[dirp->current_index];

    dirp->current_entry.d_ino = 0; 
    dirp->current_entry.d_type = entry.type();
    strncpy(dirp->current_entry.d_name, entry.name().c_str(), sizeof(dirp->current_entry.d_name) - 1);
    dirp->current_entry.d_name[sizeof(dirp->current_entry.d_name) - 1] = '\0';

    dirp->current_index++;
    
    return &dirp->current_entry;
}

int dfs_closedir(DfsDir* dirp) {
    if (!dirp) {
        errno = EBADF;
        return -1;
    }
    delete dirp; // 释放 DfsDirStream 实例
    return 0;
}

} // extern "C" 结束