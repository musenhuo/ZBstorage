#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string> // [新增] 用于存储服务器地址

// 包含我们自己的 C 语言 API 库
extern "C" {
    #include "dfs_client.h"
}

// [新增] 用于存储服务器地址的全局变量
// 我们将在 main 中解析它，在 init 中使用它
static std::string g_server_address = "0.0.0.0:8001";

// --- FUSE 回调函数 (保持不变) ---

static int dfs_fuse_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    int res = dfs_stat(path, stbuf);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int dfs_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;
    DfsDir *dirp = dfs_opendir(path);
    if (dirp == NULL) {
        return -errno;
    }
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    struct dirent *dp;
    while ((dp = dfs_readdir(dirp)) != nullptr) {
        if (filler(buf, dp->d_name, NULL, 0) != 0) {
            break;
        }
    }
    dfs_closedir(dirp);
    return 0;
}

static int dfs_fuse_open(const char *path, struct fuse_file_info *fi)
{
    int fd = dfs_open(path, fi->flags); 
    if (fd < 0) {
        return -errno;
    }
    fi->fh = fd;
    return 0;
}

static int dfs_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int fd = dfs_open(path, fi->flags, mode);
    if (fd < 0) {
        return -errno;
    }
    fi->fh = fd;
    return 0;
}

static int dfs_fuse_read(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi)
{
    (void) path;
    ssize_t bytes_read = dfs_pread(fi->fh, buf, size, offset);
    if (bytes_read == -1) {
        return -errno;
    }
    return bytes_read;
}

static int dfs_fuse_write(const char *path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi)
{
    (void) path;
    ssize_t bytes_written = dfs_pwrite(fi->fh, buf, size, offset);
    if (bytes_written == -1) {
        return -errno;
    }
    return bytes_written;
}

static int dfs_fuse_release(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    int res = dfs_close(fi->fh);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

// --- [新函数] FUSE 的 init 回调 ---
/**
 * @brief FUSE 初始化回调
 * 这个函数在 fuse_main 完成 fork() 之后、开始处理请求之前被调用。
 * 这是初始化 bRPC 客户端的唯一安全时机。
 */
void* dfs_fuse_init(struct fuse_conn_info *conn)
{
    // 此时，我们 100% 位于正确的子进程中
    printf("Initializing DFS client (post-fork), connecting to %s...\n", g_server_address.c_str());
    
    // 1. 初始化我们的 bRPC 库
    dfs_init(g_server_address.c_str());

    printf("DFS client initialized.\n");
    
    // 我们不需要返回任何私有数据
    return NULL; 
}

// [修改] 注册回调函数，添加 .init
// [FIXED] Correct struct initialization order
static struct fuse_operations dfs_fuse_oper = {
    .getattr = dfs_fuse_getattr,
    .open    = dfs_fuse_open,
    .read    = dfs_fuse_read,
    .write   = dfs_fuse_write,
    .release = dfs_fuse_release,
    .readdir = dfs_fuse_readdir,
    .init    = dfs_fuse_init,    // <-- .init comes BEFORE .create
    .create  = dfs_fuse_create,
};

int main(int argc, char *argv[])
{
    // [修改]
    // 不要在这里调用 dfs_init()！
    // 它将由 fuse_operations 中的 .init 回调安全地调用。

    // TODO: 最好在这里解析 argv 来查找 -o server=... 选项，
    // 并更新 g_server_address 变量。
    // (为保持简单，我们暂时跳过这一步)

    printf("Starting FUSE... (Press Ctrl-C to unmount)\n");
    
    // 2. 启动 FUSE 主循环
    // fuse_main 会在内部调用我们注册的 dfs_fuse_init
    return fuse_main(argc, argv, &dfs_fuse_oper, NULL);
}