// fuse_client.cc
#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <iostream>
#include <string>
#include <cstring>
#include <brpc/channel.h>
#include "posix.pb.h"

// 客户端状态，包含到MDS和DS的bRPC channel
struct ClientState {
    brpc::Channel mds_channel;
    brpc::Channel ds_channel;
};

// --- FUSE 回调函数实现 ---

static int distfs_getattr(const char* path, struct stat* stbuf) {
    ClientState* state = (ClientState*)fuse_get_context()->private_data;
    distfs::MdsService_Stub stub(&state->mds_channel);
    
    distfs::GetAttrRequest request;
    distfs::GetAttrResponse response;
    brpc::Controller cntl;

    request.set_path(path);
    stub.GetAttr(&cntl, &request, &response, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << "bRPC failed: " << cntl.ErrorText();
        return -EIO;
    }
    
    if (response.status().code() != 0) {
        return response.status().code();
    }

    memset(stbuf, 0, sizeof(struct stat));
    const auto& stat_info = response.stat_info();
    stbuf->st_ino = stat_info.ino();
    stbuf->st_mode = stat_info.mode();
    stbuf->st_nlink = stat_info.nlink();
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    stbuf->st_size = stat_info.size();
    stbuf->st_atime = stat_info.atime();
    stbuf->st_mtime = stat_info.mtime();
    stbuf->st_ctime = stat_info.ctime();
    
    return 0;
}

static int distfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info* fi) {
    ClientState* state = (ClientState*)fuse_get_context()->private_data;
    distfs::MdsService_Stub stub(&state->mds_channel);

    distfs::ReadDirRequest request;
    distfs::ReadDirResponse response;
    brpc::Controller cntl;

    request.set_path(path);
    stub.ReadDir(&cntl, &request, &response, nullptr);

    if (cntl.Failed() || response.status().code() != 0) {
        return -EIO;
    }

    for (const auto& entry : response.entries()) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = entry.ino();
        st.st_mode = (entry.type() == DT_DIR) ? S_IFDIR : S_IFREG;
        if (filler(buf, entry.name().c_str(), &st, 0))
            break;
    }

    return 0;
}

static int distfs_mkdir(const char* path, mode_t mode) {
    ClientState* state = (ClientState*)fuse_get_context()->private_data;
    distfs::MdsService_Stub stub(&state->mds_channel);
    
    distfs::MkDirRequest request;
    distfs::StatusResponse response;
    brpc::Controller cntl;

    request.set_path(path);
    request.set_mode(mode);
    stub.MkDir(&cntl, &request, &response, nullptr);

    if (cntl.Failed()) return -EIO;
    return response.code();
}

static int distfs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    ClientState* state = (ClientState*)fuse_get_context()->private_data;
    distfs::MdsService_Stub stub(&state->mds_channel);
    
    distfs::CreateRequest request;
    distfs::CreateResponse response;
    brpc::Controller cntl;

    request.set_path(path);
    request.set_flags(fi->flags);
    request.set_mode(mode);

    stub.Create(&cntl, &request, &response, nullptr);

    if (cntl.Failed() || response.status().code() != 0) {
        return -EIO; // 简化错误处理
    }

    fi->fh = response.fh(); // 将inode number存入文件句柄
    return 0;
}

static int distfs_write(const char* path, const char* buf, size_t size, off_t offset,
                        struct fuse_file_info* fi) {
    ClientState* state = (ClientState*)fuse_get_context()->private_data;
    distfs::DataService_Stub stub(&state->ds_channel);

    distfs::WriteRequest request;
    distfs::WriteResponse response;
    brpc::Controller cntl;

    request.set_ino(fi->fh); // 从文件句柄获取inode
    request.set_offset(offset);
    request.set_data(buf, size);
    
    stub.Write(&cntl, &request, &response, nullptr);

    if (cntl.Failed() || response.status().code() != 0) {
        return -EIO;
    }
    
    return response.bytes_written();
}

static int distfs_read(const char* path, char* buf, size_t size, off_t offset,
                       struct fuse_file_info* fi) {
    ClientState* state = (ClientState*)fuse_get_context()->private_data;
    distfs::DataService_Stub stub(&state->ds_channel);

    distfs::ReadRequest request;
    distfs::ReadResponse response;
    brpc::Controller cntl;

    request.set_ino(fi->fh);
    request.set_offset(offset);
    request.set_size(size);
    
    stub.Read(&cntl, &request, &response, nullptr);

    if (cntl.Failed() || response.status().code() != 0) {
        return -EIO;
    }
    
    memcpy(buf, response.data().c_str(), response.data().length());
    return response.data().length();
}

// FUSE 操作函数表的定义
static struct fuse_operations distfs_oper = {};

void setup_operations() {
    distfs_oper.getattr = distfs_getattr;
    distfs_oper.readdir = distfs_readdir;
    distfs_oper.mkdir   = distfs_mkdir;
    distfs_oper.create  = distfs_create;
    distfs_oper.write   = distfs_write;
    distfs_oper.read    = distfs_read;
    // ... 其他操作如 unlink, rmdir, open, release 也应在这里填充
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <mds_addr> <ds_addr> <mount_point> [fuse_options]\n";
        return 1;
    }
    
    setup_operations();

    ClientState state;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.connection_type = "single";
    options.timeout_ms = 2000;
    options.max_retry = 3;

    if (state.mds_channel.Init(argv[1], &options) != 0) {
        LOG(ERROR) << "Failed to initialize mds channel";
        return -1;
    }
    if (state.ds_channel.Init(argv[2], &options) != 0) {
        LOG(ERROR) << "Failed to initialize ds channel";
        return -1;
    }

    // 调整argc/argv以传递给fuse_main
    char* fuse_argv[argc - 2];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = argv[3];
    for (int i = 4; i < argc; ++i) {
        fuse_argv[i-2] = argv[i];
    }

    // 将我们的ClientState传递给FUSE上下文
    return fuse_main(argc - 2, fuse_argv, &distfs_oper, &state);
}