#include "../inode/inode.h"

struct DirectoryEntry {
    uint64_t inode;      // 64位inode号（直接存储）
    uint16_t rec_len;    // 记录长度（含填充）
    uint8_t name_len;    // 文件名长度
    FileType file_type;  // 保留自定义枚举类型
    char name[ZB_NAME_MAX];      // 文件名（无终止符）

    // 构造函数（文件名自动截断至255字节）
    DirectoryEntry(const std::string& n, uint64_t i, FileType type) 
        : inode(i), file_type(type) 
    {
        // 处理文件名（不存储终止符）
        name_len = static_cast<uint8_t>(std::min(n.size(), size_t(255)));
        memcpy(name, n.c_str(), name_len);  // 避免strncpy的自动补\0

        // 计算rec_len（8字节对齐）
        rec_len = offsetof(DirectoryEntry, name) + name_len;
        rec_len = (rec_len + 7) & ~0x7;  // 8字节对齐
    }

};

struct ZBSS_dirent {
    ino_t d_ino;           // 文件inode号
    off_t d_off;           // 目录偏移量
    unsigned short d_reclen; // 记录长度
    unsigned char d_type;   // 文件类型
    char d_name[256];       // 文件名
};

// @wbl 目录流
struct ZBSS_DIR {
    std::shared_ptr<Inode> inode;
    std::shared_ptr<Volume> volume;
    std::vector<DirectoryEntry> entries;
    size_t current_offset = 0;
};