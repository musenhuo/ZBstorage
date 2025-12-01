#include <gflags/gflags.h>
#include <brpc/server.h>
#include <butil/logging.h> // [修改] 使用 butil/logging.h
#include <string>          // [新增] 
#include <errno.h>
#include <bvar/bvar.h> 
#include "posix.pb.h"


// 包含 C/C++ 系统头文件
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

DEFINE_int32(port, 8001, "TCP Port of this server");

// [新增] 定义服务器的后端存储根目录
const std::string g_server_root = "/tmp/my_dfs_storage";

// --- [新添加] 定义一个全局 bvar 计数器 ---
// bvar::Adder<int> 是一种高性能的原子计数器
// "dfs_stat_total" 是它在 Prometheus 中的指标名称
// "Total number of Stat() calls" 是指标的描述
bvar::Adder<int64_t> g_testA("testA");
bvar::Adder<int64_t> g_testB("testB");


/**
 * @brief (不安全的) 路径拼接函数
 * 将 FUSE 客户端发来的相对路径 (如 "/" 或 "/test.txt")
 * 转换为服务器上的绝对路径 (如 "/tmp/my_dfs_storage" 或 "/tmp/my_dfs_storage/test.txt")
 */
std::string build_server_path(const std::string& client_path) {
    if (client_path == "/") {
        return g_server_root;
    }
    // 注意：这只是一个示例，没有防止 ".." 目录遍历攻击
    return g_server_root + client_path;
}

namespace posix {

class PosixServiceImpl : public PosixService {
public:
    PosixServiceImpl() = default;
    virtual ~PosixServiceImpl() = default;

    virtual void Stat(google::protobuf::RpcController* cntl_base,
                      const StatRequest* req,
                      StatResponse* res,
                      google::protobuf::Closure* done) {
        int r = butil::fast_rand() % 10;
        if (r < 4) {
            g_testA << 1;
        } else {
            g_testB << 1;
        }
        brpc::ClosureGuard done_guard(done);
        
        // [修改] 使用 build_server_path 转换路径
        std::string server_path = build_server_path(req->path());
        
        struct stat st;
        if (::stat(server_path.c_str(), &st) != 0) {
            res->set_error(errno); // 返回本地 OS 的 errno
        } else {
            res->set_error(0);
            auto* info = res->mutable_stat_info();
            info->set_mode(st.st_mode);
            info->set_size(st.st_size);
            info->set_atime(st.st_atime);
            info->set_mtime(st.st_mtime);
            info->set_ctime(st.st_ctime);
            info->set_nlink(st.st_nlink);
            info->set_uid(st.st_uid);
            info->set_gid(st.st_gid);
        }
    }

    virtual void Open(google::protobuf::RpcController* cntl_base,
                      const OpenRequest* req,
                      OpenResponse* res,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        
        // [修改] 使用 build_server_path 转换路径
        std::string server_path = build_server_path(req->path());
        
        int fd = ::open(server_path.c_str(), req->flags(), req->mode());
        if (fd < 0) {
            res->set_error(errno);
        } else {
            res->set_error(0);
            res->set_fd(fd);
        }
    }

    virtual void PRead(google::protobuf::RpcController* cntl_base,
                       const PReadRequest* req,
                       PReadResponse* res,
                       google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        std::string buffer(req->count(), '\0');
        ssize_t bytes = ::pread(req->fd(), &buffer[0], req->count(), req->offset());
        if (bytes < 0) {
            res->set_error(errno);
        } else {
            res->set_error(0);
            res->set_bytes_read(bytes);
            res->set_data(buffer.data(), bytes);
        }
    }

    virtual void PWrite(google::protobuf::RpcController* cntl_base,
                        const PWriteRequest* req,
                        PWriteResponse* res,
                        google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        ssize_t bytes = ::pwrite(req->fd(), req->data().data(), req->data().size(), req->offset());
        if (bytes < 0) {
            res->set_error(errno);
        } else {
            res->set_error(0);
            res->set_bytes_written(bytes);
        }
    }

    virtual void Close(google::protobuf::RpcController* cntl_base,
                       const CloseRequest* req,
                       CloseResponse* res,
                       google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        if (::close(req->fd()) != 0) {
            res->set_error(errno);
        } else {
            res->set_error(0);
        }
    }

    virtual void ReadDir(google::protobuf::RpcController* cntl_base,
                         const ReadDirRequest* req,
                         ReadDirResponse* res,
                         google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        
        // [修改] 使用 build_server_path 转换路径
        std::string server_path = build_server_path(req->path());

        DIR* dirp = ::opendir(server_path.c_str());
        if (!dirp) {
            res->set_error(errno);
            return;
        }

        res->set_error(0);
        struct dirent* dp;
        while ((dp = ::readdir(dirp)) != nullptr) {
            if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
                continue;
            }
            auto* entry = res->add_entries();
            entry->set_name(dp->d_name);
            entry->set_type(dp->d_type);
        }
        ::closedir(dirp);
    }
};

} // namespace posix

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // [新增] 在服务器启动时创建根目录
    mkdir(g_server_root.c_str(), 0755);
    LOG(INFO) << "Server root directory is: " << g_server_root;
    
    brpc::Server server;
    posix::PosixServiceImpl posix_service;

    if (server.AddService(&posix_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add service";
        return -1;
    }

    brpc::ServerOptions options;
    options.has_builtin_services = true;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Failed to start server";
        return -1;
    }

    LOG(INFO) << "Posix RPC Server is running on port " << FLAGS_port;
    server.RunUntilAskedToQuit();
    return 0;
}