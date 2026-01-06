#include "DfsClient.h"

#include <brpc/controller.h>
#include <cstring>
#include <unistd.h>

#include "mds.pb.h"
#include "storage_node.pb.h"

DfsClient::DfsClient(MountConfig cfg)
    : cfg_(std::move(cfg)), rpc_(std::make_unique<RpcClients>(cfg_)) {}

bool DfsClient::Init() {
    return rpc_->Init();
}

int DfsClient::StatusToErrno(rpc::StatusCode code) const {
    switch (code) {
        case rpc::STATUS_SUCCESS: return 0;
        case rpc::STATUS_INVALID_ARGUMENT: return EINVAL;
        case rpc::STATUS_NODE_NOT_FOUND: return ENOENT;
        case rpc::STATUS_IO_ERROR: return EIO;
        case rpc::STATUS_NETWORK_ERROR: return ECOMM;
        default: return EIO;
    }
}

bool DfsClient::PopulateStatFromInode(const rpc::FindInodeReply& reply, struct stat* st) const {
    if (!st) return false;
    std::memset(st, 0, sizeof(struct stat));
    // Default regular file with 0644 perms; size unknown (set 0).
    st->st_mode = S_IFREG | 0644;
    st->st_size = 0;
    st->st_nlink = 1;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_atime = 0;
    st->st_mtime = 0;
    st->st_ctime = 0;
    return true;
}

rpc::StatusCode DfsClient::LookupInode(const std::string& path, InodeInfo& out_info) {
    if (!rpc_ || !rpc_->mds()) return rpc::STATUS_NETWORK_ERROR;
    rpc::PathRequest req;
    rpc::FindInodeReply resp;
    brpc::Controller cntl;
    cntl.set_timeout_ms(cfg_.rpc_timeout_ms);
    req.set_path(path);
    rpc_->mds()->FindInode(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) {
        std::cerr << "[Client] FindInode failed path=" << path << " err=" << cntl.ErrorText() << std::endl;
        return rpc::STATUS_NETWORK_ERROR;
    }
    auto code = StatusUtils::NormalizeCode(resp.status().code());
    if (code != rpc::STATUS_SUCCESS) {
        std::cerr << "[Client] FindInode failed path=" << path
                  << " code=" << static_cast<int>(code)
                  << " msg=" << resp.status().message() << std::endl;
        return code;
    }
    out_info.inode = 0;
    // For safety, also call LookupIno to get inode number.
    rpc::LookupReply lresp;
    brpc::Controller lcntl;
    lcntl.set_timeout_ms(cfg_.rpc_timeout_ms);
    rpc_->mds()->LookupIno(&lcntl, &req, &lresp, nullptr);
    if (!lcntl.Failed()) {
        auto lcode = StatusUtils::NormalizeCode(lresp.status().code());
        if (lcode == rpc::STATUS_SUCCESS) {
            out_info.inode = lresp.inode();
        } else {
            std::cerr << "[Client] LookupIno failed path=" << path << " code=" << static_cast<int>(lcode) << std::endl;
            return lcode;
        }
    } else {
        std::cerr << "[Client] LookupIno RPC failed path=" << path << " err=" << lcntl.ErrorText() << std::endl;
        return rpc::STATUS_NETWORK_ERROR;
    }
    out_info.node_id = !resp.node_id().empty() ? resp.node_id() : resp.volume_id();
    return rpc::STATUS_SUCCESS;
}

int DfsClient::GetAttr(const std::string& path, struct stat* st) {
    if (!rpc_ || !rpc_->mds()) return -ECOMM;
    InodeInfo info;
    auto code = LookupInode(path, info);
    if (code != rpc::STATUS_SUCCESS) {
        std::cerr << "[Client] Open LookupInode failed path=" << path
                  << " code=" << static_cast<int>(code) << std::endl;
        return -StatusToErrno(code);
    }

    rpc::PathRequest req;
    rpc::FindInodeReply resp;
    brpc::Controller cntl;
    cntl.set_timeout_ms(cfg_.rpc_timeout_ms);
    req.set_path(path);
    rpc_->mds()->FindInode(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) {
        std::cerr << "[Client] GetAttr FindInode RPC failed path=" << path << " err=" << cntl.ErrorText() << std::endl;
        return -ECOMM;
    }
    if (!PopulateStatFromInode(resp, st)) return -EIO;
    return 0;
}

int DfsClient::ReadDir(const std::string& path, void* buf, fuse_fill_dir_t filler) {
    if (!rpc_ || !rpc_->mds()) return -ECOMM;
    rpc::PathRequest req;
    rpc::DirectoryListReply resp;
    brpc::Controller cntl;
    cntl.set_timeout_ms(cfg_.rpc_timeout_ms);
    req.set_path(path);
    rpc_->mds()->Ls(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) {
        std::cerr << "[Client] ReadDir RPC failed path=" << path << " err=" << cntl.ErrorText() << std::endl;
        return -ECOMM;
    }
    auto code = StatusUtils::NormalizeCode(resp.status().code());
    if (code != rpc::STATUS_SUCCESS) return -StatusToErrno(code);

    filler(buf, ".", nullptr, 0);
    filler(buf, "..", nullptr, 0);
    for (const auto& entry : resp.entries()) {
        filler(buf, entry.name().c_str(), nullptr, 0);
    }
    return 0;
}

int DfsClient::Open(const std::string& path, int flags, int& out_fd) {
    InodeInfo info;
    auto code = LookupInode(path, info);
    if (code != rpc::STATUS_SUCCESS) return -StatusToErrno(code);
    int fd = next_fd_++;
    fd_info_[fd] = info;
    out_fd = fd;
    return 0;
}

int DfsClient::Create(const std::string& path, int flags, mode_t mode, int& out_fd) {
    if (!rpc_ || !rpc_->mds()) return -ECOMM;
    rpc::PathModeRequest creq;
    rpc::Status cresp;
    brpc::Controller ccntl;
    ccntl.set_timeout_ms(cfg_.rpc_timeout_ms);
    creq.set_path(path);
    creq.set_mode(static_cast<uint32_t>(mode));
    rpc_->mds()->CreateFile(&ccntl, &creq, &cresp, nullptr);
    if (ccntl.Failed()) {
        std::cerr << "[Client] CreateFile RPC failed path=" << path << " err=" << ccntl.ErrorText() << std::endl;
        return -ECOMM;
    }
    auto ccode = StatusUtils::NormalizeCode(cresp.code());
    if (ccode != rpc::STATUS_SUCCESS) {
        std::cerr << "[Client] CreateFile failed path=" << path
                  << " code=" << static_cast<int>(ccode)
                  << " msg=" << cresp.message() << std::endl;
        return -StatusToErrno(ccode);
    }
    return Open(path, flags, out_fd);
}

int DfsClient::Read(int fd, char* buf, size_t size, off_t offset, ssize_t& out_bytes) {
    if (!rpc_ || !rpc_->srm()) return -ECOMM;
    auto it = fd_info_.find(fd);
    if (it == fd_info_.end()) return -EBADF;

    storagenode::ReadRequest req;
    storagenode::ReadReply resp;
    brpc::Controller cntl;
    cntl.set_timeout_ms(cfg_.rpc_timeout_ms);
    const std::string& node_id = it->second.node_id.empty() ? cfg_.default_node_id : it->second.node_id;
    req.set_node_id(node_id);
    req.set_chunk_id(static_cast<uint64_t>(it->second.inode));
    req.set_offset(static_cast<uint64_t>(offset));
    req.set_length(static_cast<uint64_t>(size));
    rpc_->srm()->Read(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) {
        std::cerr << "[Client] Read RPC failed: " << cntl.ErrorText() << std::endl;
        return -ECOMM;
    }
    auto code = StatusUtils::NormalizeCode(resp.status().code());
    if (code != rpc::STATUS_SUCCESS) {
        std::cerr << "[Client] Read failed fd=" << fd
                  << " code=" << static_cast<int>(code)
                  << " msg=" << resp.status().message() << std::endl;
        return -StatusToErrno(code);
    }
    out_bytes = static_cast<ssize_t>(resp.bytes_read());
    if (out_bytes > 0 && static_cast<size_t>(out_bytes) <= size) {
        std::memcpy(buf, resp.data().data(), static_cast<size_t>(out_bytes));
    }
    return 0;
}

int DfsClient::Write(int fd, const char* buf, size_t size, off_t offset, ssize_t& out_bytes) {
    if (!rpc_ || !rpc_->srm()) return -ECOMM;
    auto it = fd_info_.find(fd);
    if (it == fd_info_.end()) return -EBADF;

    storagenode::WriteRequest req;
    storagenode::WriteReply resp;
    brpc::Controller cntl;
    cntl.set_timeout_ms(cfg_.rpc_timeout_ms);
    const std::string& node_id = it->second.node_id.empty() ? cfg_.default_node_id : it->second.node_id;
    req.set_node_id(node_id);
    req.set_chunk_id(static_cast<uint64_t>(it->second.inode));
    req.set_offset(static_cast<uint64_t>(offset));
    req.set_data(buf, size);
    req.set_checksum(0);
    req.set_flags(0);
    req.set_mode(0644);

    rpc_->srm()->Write(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) {
        std::cerr << "[Client] Write RPC failed: " << cntl.ErrorText() << std::endl;
        return -ECOMM;
    }
    auto code = StatusUtils::NormalizeCode(resp.status().code());
    if (code != rpc::STATUS_SUCCESS) {
        std::cerr << "[Client] Write failed fd=" << fd
                  << " code=" << static_cast<int>(code)
                  << " msg=" << resp.status().message() << std::endl;
        return -StatusToErrno(code);
    }
    out_bytes = static_cast<ssize_t>(resp.bytes_written());
    return 0;
}

int DfsClient::Close(int fd) {
    auto it = fd_info_.find(fd);
    if (it != fd_info_.end()) {
        fd_info_.erase(it);
    }
    return 0;
}
