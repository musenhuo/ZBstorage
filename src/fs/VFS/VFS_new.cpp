#include "VFS_new.h"
#include <stdexcept>
#include <utility>
#include "../io/LocalStorageGateway.h"
#include "../../debug/ZBLog.h"

class FileSystemHandleObserver : public IHandleObserver {
public:
    explicit FileSystemHandleObserver(FileSystem* owner) : owner_(owner) {}

    void CloseHandlesForInode(uint64_t inode) override {
        if (owner_) {
            owner_->force_close_handles(inode);
        }
    }

    void detach() { owner_ = nullptr; }

private:
    FileSystem* owner_;
};

namespace {
std::shared_ptr<IVolumeRegistry> create_default_registry() {
    try {
        return make_file_volume_registry(".");
    } catch (...) {
        return nullptr;
    }
}

#ifdef ZBSS_ENABLE_LOG
void report_bool(const char* api,
                 const std::string& target,
                 bool ok,
                 const char* expectation) {
    if (ok) {
        LOGD("[VFS_new] " << api << "('" << target << "') OK | expect " << expectation);
    } else {
        LOGE("[VFS_new] " << api << "('" << target << "') FAIL | expect " << expectation);
    }
}

template <typename T>
void report_value(const char* api,
                  const std::string& target,
                  const T& value,
                  bool ok,
                  const char* expectation) {
    if (ok) {
        LOGD("[VFS_new] " << api << "('" << target << "') -> " << value << " | expect " << expectation);
    } else {
        LOGE("[VFS_new] " << api << "('" << target << "') -> " << value << " | expect " << expectation);
    }
}

void report_count(const char* api,
                  size_t count,
                  size_t bound,
                  const char* expectation) {
    bool ok = bound == 0 || count <= bound;
    if (ok) {
        LOGD("[VFS_new] " << api << " returned " << count << " entries | expect " << expectation);
    } else {
        LOGE("[VFS_new] " << api << " returned " << count << " entries | expect " << expectation);
    }
}
#else
inline void report_bool(const char*, const std::string&, bool, const char*) {}
template <typename T>
inline void report_value(const char*, const std::string&, const T&, bool, const char*) {}
inline void report_count(const char*, size_t, size_t, const char*) {}
#endif
}

FileSystem::FileSystem(bool create_new)
    : FileSystem(std::make_shared<MdsServer>(create_new),
                 create_default_registry(),
                 std::make_shared<VolumeManager>()) {}

FileSystem::FileSystem(std::shared_ptr<MdsServer> mds,
                       std::shared_ptr<IVolumeRegistry> volume_registry,
                       std::shared_ptr<VolumeManager> volume_manager)
    : mds_(std::move(mds)),
      volume_manager_(volume_manager ? std::move(volume_manager)
                                   : std::make_shared<VolumeManager>()) {
    if (!mds_) {
        throw std::invalid_argument("FileSystem requires a valid MdsServer instance");
    }
    if (volume_registry) {
        mds_->set_volume_registry(volume_registry);
    }
    if (volume_manager_) {
        mds_->set_volume_manager(volume_manager_);
    }
    if (fd_bitmap_.empty()) {
        fd_bitmap_.resize(4096, true);         // 初始容量
        for (int fd : {0, 1, 2}) {             // 预留标准输入输出
            if (fd < static_cast<int>(fd_bitmap_.size())) {
                fd_bitmap_.reset(fd);
            }
        }
    }
    if (volume_manager_) {
        volume_manager_->set_default_gateway(std::make_shared<LocalStorageGateway>());
    }
    handle_observer_ = std::make_shared<FileSystemHandleObserver>(this);
    if (mds_) {
        mds_->set_handle_observer(std::weak_ptr<IHandleObserver>(handle_observer_));
    }
}

FileSystem::~FileSystem() {
    if (handle_observer_) {
        handle_observer_->detach();
    }
    if (mds_) {
        mds_->set_handle_observer({});
        mds_->set_volume_manager(nullptr);
    }
}

std::shared_ptr<MdsServer> FileSystem::metadata() const noexcept {
    return mds_;
}

std::shared_ptr<IVolumeRegistry> FileSystem::volume_registry() const noexcept {
    return mds_ ? mds_->volume_registry() : nullptr;
}

std::shared_ptr<VolumeManager> FileSystem::volume_manager() const noexcept {
    return volume_manager_;
}

void FileSystem::set_volume_manager(std::shared_ptr<VolumeManager> manager) {
    volume_manager_ = std::move(manager);
}

bool FileSystem::create_root_directory() {
    bool ok = mds_ && mds_->CreateRoot();
    report_bool("create_root_directory", "/", ok,
                "root inode exists and ls('/') shows '.' entry");
    return ok;
}

bool FileSystem::create_file(const std::string& path, mode_t mode) {
    bool ok = mds_ && mds_->CreateFile(path, mode);
    report_bool("create_file", path, ok,
                "file appears in ls(parent) and lookup_inode succeeds");
    return ok;
}

bool FileSystem::remove_file(const std::string& path) {
    bool ok = mds_ && mds_->RemoveFile(path);
    report_bool("remove_file", path, ok,
                "path disappears from ls(parent) and open handles are closed");
    return ok;
}

bool FileSystem::mkdir(const std::string& path, mode_t mode) {
    bool ok = mds_ && mds_->Mkdir(path, mode);
    report_bool("mkdir", path, ok,
                "new directory is listed by ls(parent) and can host entries");
    return ok;
}

bool FileSystem::rmdir(const std::string& path) {
    bool ok = mds_ && mds_->Rmdir(path);
    report_bool("rmdir", path, ok,
                "directory no longer appears in ls(parent) and lookup fails");
    return ok;
}

bool FileSystem::ls(const std::string& path) {
    bool ok = mds_ && mds_->Ls(path);
    report_bool("ls", path, ok,
                "directory entries stream to stdout for manual inspection");
    return ok;
}

uint64_t FileSystem::lookup_inode(const std::string& abs_path) const {
    uint64_t ino = mds_ ? mds_->LookupIno(abs_path) : static_cast<uint64_t>(-1);
    report_value("lookup_inode", abs_path, ino, ino != static_cast<uint64_t>(-1),
                 "valid inode id when path exists");
    return ino;
}

std::shared_ptr<Inode> FileSystem::find_inode_by_path(const std::string& path) const {
    return mds_ ? mds_->FindInodeByPath(path) : nullptr;
}

uint64_t FileSystem::get_root_inode() const {
    uint64_t ino = mds_ ? mds_->GetRootInode() : static_cast<uint64_t>(-1);
    report_value("get_root_inode", "/", ino, ino != static_cast<uint64_t>(-1),
                 "root inode should be fixed and non-negative");
    return ino;
}

std::vector<uint64_t> FileSystem::collect_cold_inodes(size_t max_candidates,
                                                      size_t min_age_windows) {
    auto list = mds_ ? mds_->CollectColdInodes(max_candidates, min_age_windows)
                     : std::vector<uint64_t>{};
    report_count("collect_cold_inodes", list.size(), max_candidates,
                 "count stays within requested bound");
    return list;
}

std::shared_ptr<boost::dynamic_bitset<>> FileSystem::collect_cold_inodes_bitmap(size_t min_age_windows) {
    auto bitmap = mds_ ? mds_->CollectColdInodesBitmap(min_age_windows)
                       : nullptr;
    size_t size = bitmap ? bitmap->size() : 0;
    bool ok = bitmap && mds_ && size >= mds_->GetTotalInodes();
    report_value("collect_cold_inodes_bitmap", std::to_string(min_age_windows), size, ok,
                 "bitset exists and covers at least total inode space");
    return bitmap;
}

std::vector<uint64_t> FileSystem::collect_cold_inodes_by_atime_percent(double percent) {
    auto list = mds_ ? mds_->CollectColdInodesByAtimePercent(percent)
                     : std::vector<uint64_t>{};
    report_count("collect_cold_inodes_by_atime_percent", list.size(),
                 mds_ ? mds_->GetTotalInodes() : 0,
                 "result size is bounded by total inode count");
    return list;
}

void FileSystem::rebuild_inode_table() {
    if (mds_) {
        mds_->RebuildInodeTable();
        report_bool("rebuild_inode_table", "/", true,
                    "in-memory path cache refreshed from metadata store");
    } else {
        report_bool("rebuild_inode_table", "/", false,
                    "metadata server must exist");
    }
}

bool FileSystem::register_volume(const std::shared_ptr<Volume>& vol,
                                 VolumeType type,
                                 int* out_index,
                                 bool persist_now) {
    if (!vol) {
        return false;
    }

    bool ok = true;
    if (mds_) {
        ok = mds_->RegisterVolume(vol, type, out_index, persist_now);
    } else {
        ok = false;
    }
    if (ok && volume_manager_) {
        volume_manager_->register_volume(vol);
    }
    report_bool("register_volume", vol ? vol->uuid() : "<null>", ok,
                "volume persists to registry and becomes IO target");
    return ok;
}

bool FileSystem::register_volume(const std::shared_ptr<Volume>& vol) {
    if (!vol) {
        report_bool("register_volume", "<null>", false,
                    "non-null volume is required");
        return false;
    }
    if (volume_manager_) {
        volume_manager_->register_volume(vol);
    }
    report_bool("register_volume", vol->uuid(), true,
                "volume registered for IO dispatch only");
    return true;
}

bool FileSystem::startup() {
    bool ok = true;
    if (mds_) {
        ok = mds_->CreateRoot() && ok;
    }
    if (auto registry = volume_registry()) {
        ok = registry->startup() && ok;
    }
    report_bool("startup", "/", ok,
                "root directory initialized and volumes restored");
    return ok;
}

bool FileSystem::shutdown() {
    bool ok = true;
    if (auto registry = volume_registry()) {
        ok = registry->shutdown() && ok;
    }
    report_bool("shutdown", "/", ok,
                "volume metadata flushed to disk");
    return ok;
}

int FileSystem::acquire_fd_locked() {
    size_t pos = fd_bitmap_.find_first();
    if (pos == boost::dynamic_bitset<>::npos) {
        const size_t old_size = fd_bitmap_.size();
        const size_t new_size = std::max<size_t>(old_size ? old_size * 2 : 8, 8);
        fd_bitmap_.resize(new_size, true);
        pos = fd_bitmap_.find_first();
        if (pos == boost::dynamic_bitset<>::npos) {
            return -1;
        }
        // 再次预留标准 fd，防止扩容时被置为可用
        for (int fd : {0, 1, 2}) {
            if (fd < static_cast<int>(fd_bitmap_.size())) {
                fd_bitmap_.reset(fd);
            }
        }
        if (pos < 3) {
            pos = fd_bitmap_.find_next(2);
            if (pos == boost::dynamic_bitset<>::npos) return -1;
        }
    } else if (pos < 3) {
        pos = fd_bitmap_.find_next(2);
        if (pos == boost::dynamic_bitset<>::npos) {
            fd_bitmap_.reset(0);
            fd_bitmap_.reset(1);
            fd_bitmap_.reset(2);
            return -1;
        }
    }
    fd_bitmap_.reset(pos);
    return static_cast<int>(pos);
}

void FileSystem::release_fd_locked(int fd) {
    if (fd >= 0 && static_cast<size_t>(fd) < fd_bitmap_.size()) {
        fd_bitmap_.set(static_cast<size_t>(fd));
    }
}

int FileSystem::allocate_fd_locked(std::shared_ptr<Inode> inode, int flags) {
    int fd = acquire_fd_locked();
    if (fd < 0) {
        return -1;
    }
    fd_table_.emplace(fd, FdTableEntry(std::move(inode), flags));
    return fd;
}

FdTableEntry* FileSystem::find_fd_locked(int fd) {
    auto it = fd_table_.find(fd);
    return it == fd_table_.end() ? nullptr : &it->second;
}

int FileSystem::open(const std::string& path, int flags, mode_t mode) {
    if (!mds_) return -1;

    std::shared_ptr<Inode> inode = mds_->FindInodeByPath(path);
    if (!inode) {
        if (!(flags & CREATE)) {
            return -1;
        }
        if (!mds_->CreateFile(path, mode)) {
            return -1;
        }
        inode = mds_->FindInodeByPath(path);
    } else if ((flags & TRUNCATE) && !mds_->TruncateFile(path)) {
        return -1;
    }

    if (!inode) {
        report_value("open", path, -1, false,
                     "expected inode exists or is created before open");
        return -1;
    }

    std::lock_guard lk(fd_mutex_);
    int fd = allocate_fd_locked(std::move(inode), flags);
    report_value("open", path, fd, fd >= 0,
                 "fd usable for subsequent read/write/seek");
    return fd;
}

int FileSystem::close(int fd) {
    int rv = shutdown_fd(fd);
    report_value("close", std::to_string(fd), rv, rv == 0,
                 "fd removed from table and further IO rejected");
    return rv;
}

int FileSystem::shutdown_fd(int fd) {
    std::lock_guard lk(fd_mutex_);
    auto it = fd_table_.find(fd);
    if (it == fd_table_.end()) {
        report_value("shutdown_fd", std::to_string(fd), -1, false,
                     "fd must exist before shutdown");
        return -1;
    }
    if (--it->second.ref_count <= 0) {
        fd_table_.erase(it);
        release_fd_locked(fd);
    }
    report_value("shutdown_fd", std::to_string(fd), 0, true,
                 "no further reads/writes allowed on this fd");
    return 0;
}

off_t FileSystem::seek(int fd, off_t offset, int whence) {
    std::lock_guard lk(fd_mutex_);
    FdTableEntry* entry = find_fd_locked(fd);
    if (!entry || !entry->inode) {
        report_value("seek", std::to_string(fd), -1, false,
                     "fd must be valid before seek");
        return -1;
    }

    off_t base = 0;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<off_t>(entry->offset); break;
    case SEEK_END: base = static_cast<off_t>(entry->inode->getFileSize()); break;
    default: return -1;
    }

    off_t target = base + offset;
    if (target < 0) {
        report_value("seek", std::to_string(fd), -1, false,
                     "result offset must stay non-negative");
        return -1;
    }

    entry->offset = static_cast<size_t>(target);
    report_value("seek", std::to_string(fd), target, true,
                 "subsequent read/write begin at reported offset");
    return target;
}

ssize_t FileSystem::write(int fd, const char* buf, size_t count) {
    if (count == 0) return 0;
    if (!buf || !volume_manager_ || !mds_) return -1;

    std::shared_ptr<Inode> inode;
    size_t offset = 0;
    {
        std::lock_guard lk(fd_mutex_);
        FdTableEntry* entry = find_fd_locked(fd);
        if (!entry || !entry->inode) {
            report_value("write", std::to_string(fd), -1, false,
                         "fd must reference inode before write");
            return -1;
        }
        if (entry->flags & MO_RDONLY) {
            report_value("write", std::to_string(fd), -1, false,
                         "fd opened read-only cannot be written");
            return -1;
        }
        if (entry->flags & MO_APPEND) {
            offset = entry->inode->getFileSize();
        } else {
            offset = entry->offset;
        }
        inode = entry->inode;
    }

    ssize_t written = volume_manager_->write_file(inode, offset, buf, count);
    if (written <= 0) {
        report_value("write", std::to_string(fd), written, false,
                     "positive byte count indicates volume write success");
        return written;
    }

    {
        std::lock_guard lk(fd_mutex_);
        FdTableEntry* entry = find_fd_locked(fd);
        if (entry && entry->inode == inode) {
            entry->offset = (entry->flags & MO_APPEND)
                ? inode->getFileSize()
                : entry->offset + static_cast<size_t>(written);
        }
    }

    InodeTimestamp now;
    inode->setFmTime(now);
    inode->setFaTime(now);
    inode->setFcTime(now);
    mds_->WriteInode(inode->inode, *inode);
    report_value("write", std::to_string(fd), written, true,
                 "bytes durable; read should return same count");
    return written;
}

ssize_t FileSystem::read(int fd, char* buf, size_t count) {
    if (count == 0) return 0;
    if (!buf || !volume_manager_ || !mds_) return -1;

    std::shared_ptr<Inode> inode;
    size_t offset = 0;
    {
        std::lock_guard lk(fd_mutex_);
        FdTableEntry* entry = find_fd_locked(fd);
        if (!entry || !entry->inode) {
            report_value("read", std::to_string(fd), -1, false,
                         "fd must reference inode before read");
            return -1;
        }
        if (entry->flags & MO_WRONLY) {
            report_value("read", std::to_string(fd), -1, false,
                         "write-only fd cannot be read");
            return -1;
        }
        offset = entry->offset;
        inode = entry->inode;
    }

    ssize_t read_bytes = volume_manager_->read_file(inode, offset, buf, count);
    if (read_bytes < 0) {
        report_value("read", std::to_string(fd), read_bytes, false,
                     "volume read should return non-negative byte count");
        return read_bytes;
    }

    {
        std::lock_guard lk(fd_mutex_);
        FdTableEntry* entry = find_fd_locked(fd);
        if (entry && entry->inode == inode) {
            entry->offset += static_cast<size_t>(read_bytes);
        }
    }

    InodeTimestamp now;
    inode->setFaTime(now);
    mds_->WriteInode(inode->inode, *inode);
    report_value("read", std::to_string(fd), read_bytes, true,
                 "buffer now holds bytes written earlier");
    return read_bytes;
}

void FileSystem::force_close_handles(uint64_t inode) {
    std::lock_guard lk(fd_mutex_);
    for (auto it = fd_table_.begin(); it != fd_table_.end();) {
        if (it->second.inode && it->second.inode->inode == inode) {
            release_fd_locked(it->first);
            it = fd_table_.erase(it);
        } else {
            ++it;
        }
    }
}