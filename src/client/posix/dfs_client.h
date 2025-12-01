#pragma once

#include <sys/stat.h> // 为了使用 struct stat
#include <sys/types.h>
#include <dirent.h>   // 为了 d_type

// 必须暴露给外部的结构体，用于 readdir
struct DfsDirStream;
typedef struct DfsDirStream DfsDir;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化分布式文件系统客户端
 * @param server_address 服务器地址 (例如 "0.0.0.0:8001")
 */
void dfs_init(const char* server_address);

// --- POSIX API 封装 ---

int dfs_stat(const char* path, struct stat* buf);

// 支持 O_CREAT 时的 mode 参数
int dfs_open(const char* path, int flags, ...); 

ssize_t dfs_read(int fd, void* buf, size_t count);

ssize_t dfs_write(int fd, const void* buf, size_t count);

ssize_t dfs_pread(int fd, void* buf, size_t count, off_t offset);

ssize_t dfs_pwrite(int fd, const void* buf, size_t count, off_t offset);

int dfs_close(int fd);

DfsDir* dfs_opendir(const char* path);

struct dirent* dfs_readdir(DfsDir* dirp);

int dfs_closedir(DfsDir* dirp);

#ifdef __cplusplus
}
#endif