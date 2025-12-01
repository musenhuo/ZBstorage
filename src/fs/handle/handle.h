#include <climits>
#include <cstdint>
#include <iostream>
#include "../../mds/inode/inode.h"
// 文件打开模式标志
// 与POSIX标准兼容的文件打开标志
enum OpenFlags {
    // 访问模式标志 (必选一个)
    READ        = 0x0000,  // 对应O_RDONLY
    WRITE       = 0x0001,  // 对应O_WRONLY  
    READWRITE   = 0x0002,  // 对应O_RDWR
    
    // 文件创建和修改标志
    CREATE      = 0x0100,  // 对应O_CREAT，如果文件不存在则创建
    TRUNCATE    = 0x0200,  // 对应O_TRUNC，打开时截断文件
    APPEND      = 0x0400,  // 对应O_APPEND，所有写入追加到文件末尾
    EXCLUSIVE   = 0x0800,  // 对应O_EXCL，与CREATE一起使用，确保创建一个新文件
    
    // 文件状态标志
    SYNC        = 0x1000,  // 对应O_SYNC，同步写入，等待物理I/O完成
    DSYNC       = 0x2000,  // 对应O_DSYNC，同步数据写入，不同步元数据
    NONBLOCK    = 0x4000,  // 对应O_NONBLOCK，非阻塞模式
    ASYNC       = 0x8000,  // 对应O_ASYNC，异步I/O模式
    
    // 目录和链接相关标志
    DIRECTORY   = 0x10000, // 对应O_DIRECTORY，确保打开的是目录
    NOFOLLOW    = 0x20000, // 对应O_NOFOLLOW，不跟随符号链接
    
    // 文件描述符相关标志
    CLOEXEC     = 0x40000, // 对应O_CLOEXEC，设置close-on-exec标志
    DIRECT      = 0x80000, // 对应O_DIRECT，直接I/O，尽可能减少缓存
    
    // 特殊用途标志
    PATH        = 0x100000,// 对应O_PATH，仅获取文件引用，不实际打开
    TMPFILE     = 0x200000,// 对应O_TMPFILE，创建临时文件
    NOCTTY      = 0x400000 // 对应O_NOCTTY，不将终端设备设为控制终端
};

// 文件锁结构（POSIX兼容）
struct FileLock {
    off_t start;     // 锁起始偏移
    off_t end;       // 锁结束偏移（0表示无界）
    pid_t pid;       // 加锁进程
    int type;        // 锁类型 F_RDLCK/F_WRLCK
    
    bool overlaps(off_t s, off_t e) const {
        if (e == 0) e = LLONG_MAX; // 无限范围
        return (s < end) && (e > start);
    }
};

// 文件描述符表项
struct FdTableEntry {
    std::shared_ptr<Inode> inode;
    size_t offset;                 // 当前读写位置
    int flags;                     // 打开标志位组合
    std::vector<FileLock> locks;   // 文件锁列表
    uint32_t ref_count = 1;        // 描述符引用计数
    
    FdTableEntry() = default; // 无参构造
    // 构造函数
    FdTableEntry(std::shared_ptr<Inode> ino, int flg) 
        : inode(ino), offset(0), flags(flg) 
    {
        if (flags & MO_APPEND) {
           offset = ino->file_size.fields.file_size;
        }
    }
    
    // 检查访问权限
    bool can_read() const {
        return flags & MO_RDONLY || flags & MO_RDWR;
    }
    
    bool can_write() const {
        return flags & MO_WRONLY || flags & MO_RDWR;
    }
};