#include "dfs_client.h" // 包含我们的封装库
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>     // 为了 O_CREAT, O_WRONLY...
#include <unistd.h>    // 为了 STDOUT_FILENO

int main(int argc, char* argv[]) {
    // 1. 初始化客户端库，指向服务器地址
    dfs_init("0.0.0.0:8001");
    printf("DFS Client initialized.\n");

    // [修改]
    // 路径应该是相对于 DFS 根目录的路径
    // FUSE 会把 ~/my-dfs/test.txt 转换为 "/test.txt"
    // 所以我们的测试程序也应该直接使用 "/test.txt"
    const char* path = "/test.txt"; 
    const char* content = "Hello Distributed File System!";

    // 2. 调用 dfs_open (创建并写入)
    // O_TRUNC 表示如果文件已存在则清空
    int fd = dfs_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        // 如果失败，打印 errno
        fprintf(stderr, "dfs_open for write failed: %s\n", strerror(errno));
        return -1;
    }
    printf("dfs_open for write succeeded, fd=%d\n", fd);

    // 3. 调用 dfs_write
    ssize_t written = dfs_pwrite(fd, content, strlen(content), 0); // [修改] 使用 pwrite
    if (written < 0) {
        fprintf(stderr, "dfs_pwrite failed: %s\n", strerror(errno));
    } else {
        printf("dfs_pwrite %ld bytes succeeded.\n", written);
    }

    // 4. 调用 dfs_close
    dfs_close(fd);

    // 5. 调用 dfs_stat 检查文件
    struct stat st;
    if (dfs_stat(path, &st) == 0) {
        printf("dfs_stat: File size is %ld\n", st.st_size);
    } else {
        fprintf(stderr, "dfs_stat failed: %s\n", strerror(errno));
    }

    // 6. 调用 dfs_open (读取)
    fd = dfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        fprintf(stderr, "dfs_open for read failed: %s\n", strerror(errno));
        return -1;
    }
    printf("dfs_open for read succeeded, fd=%d\n", fd);

    // 7. 调用 dfs_read
    char read_buf[100];
    memset(read_buf, 0, sizeof(read_buf));
    ssize_t bytes_read = dfs_pread(fd, read_buf, sizeof(read_buf) - 1, 0); // [修改] 使用 pread
    if (bytes_read < 0) {
        fprintf(stderr, "dfs_pread failed: %s\n", strerror(errno));
    } else {
        printf("dfs_pread %ld bytes: \"%s\"\n", bytes_read, read_buf);
    }

    // 8. 调用 dfs_close
    dfs_close(fd);

    // 9. 调用 dfs_opendir/readdir
    // [修改] 我们读取根目录 "/"，而不是 "/tmp"
    printf("\nReading directory /:\n");
    DfsDir* dirp = dfs_opendir("/"); 
    if (!dirp) {
        fprintf(stderr, "dfs_opendir failed: %s\n", strerror(errno));
    } else {
        struct dirent* dp;
        while ((dp = dfs_readdir(dirp)) != nullptr) {
            printf("  > %s (%s)\n", dp->d_name, (dp->d_type == DT_DIR) ? "DIR" : "FILE");
        }
        dfs_closedir(dirp);
    }

    return 0;
}