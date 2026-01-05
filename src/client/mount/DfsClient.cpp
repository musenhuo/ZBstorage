#include "DfsClient.h"

#include <brpc/controller.h>
#include <cstring>
#include <unistd.h>

#include "mds.pb.h"
#include "vfs.pb.h"
#include "storage_node.pb.h"
#include "mds/inode/inode.h"

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
    if (!reply.has_inode()) return false;
    const auto& blob = reply.inode().data();
    Inode inode;
    size_t offset = 0;
    if (!Inode::deserialize(reinterpret_cast<const uint8_t*>(blob.data()), offset, inode, blob.size())) {
        return false;
    }
    std::memset(st, 0, sizeof(struct stat));
    st->st_mode = inode.file_mode.raw;
    st->st_size = static_cast<off_t>(inode.getFileSize());
    st->st_nlink = 1;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_atime = 0;
    st->st_mtime = 0;
    st->st_ctime = 0;
    return true;
}

rpc::StatusCode DfsClient::LookupInode(const std::string& path, uint64_t& out_inode) {
    if (!rpc_ || !rpc_->mds()) return rpc::STATUS_NETWORK_ERROR;
    rpc::PathRequest req;
    rpc::FindInodeReply resp;
    brpc::Controller cntl;
    req.set_path(path);
    rpc_->mds()->FindInode(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) {
        return rpc::STATUS_NETWORK_ERROR;
    }
    auto code = StatusUtils::NormalizeCode(resp.status().code());
    if (code != rpc::STATUS_SUCCESS) {
        return code;
    }
    if (!resp.has_inode()) {
        return rpc::STATUS_NODE_NOT_FOUND;
    }
    Inode inode;
    size_t offset = 0;
    const auto& blob = resp.inode().data();
    if (!Inode::deserialize(reinterpret_cast<const uint8_t*>(blob.data()), offset, inode, blob.size())) {
        return rpc::STATUS_IO_ERROR;
    }
    out_inode = inode.inode;
    return rpc::STATUS_SUCCESS;
}

int DfsClient::GetAttr(const std::string& path, struct stat* st) {
    if (!rpc_ || !rpc_->mds()) return -ECOMM;
    uint64_t ino = 0;
    auto code = LookupInode(path, ino);
    if (code != rpc::STATUS_SUCCESS) return -StatusToErrno(code);

    rpc::PathRequest req;
    rpc::FindInodeReply resp;
    brpc::Controller cntl;
    req.set_path(path);
    rpc_->mds()->FindInode(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) return -ECOMM;
    if (!PopulateStatFromInode(resp, st)) return -EIO;
    return 0;
}

int DfsClient::ReadDir(const std::string& path, void* buf, fuse_fill_dir_t filler) {
    if (!rpc_ || !rpc_->mds()) return -ECOMM;
    rpc::PathRequest req;
    rpc::DirectoryListReply resp;
    brpc::Controller cntl;
    req.set_path(path);
    rpc_->mds()->Ls(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) return -ECOMM;
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
    uint64_t ino = 0;
    auto code = LookupInode(path, ino);
    if (code != rpc::STATUS_SUCCESS) return -StatusToErrno(code);
    int fd = next_fd_++;
    fd_to_inode_[fd] = ino;
    out_fd = fd;
    return 0;
}

int DfsClient::Create(const std::string& path, int flags, mode_t mode, int& out_fd) {
    if (!rpc_ || !rpc_->mds()) return -ECOMM;
    rpc::PathModeRequest creq;
    rpc::Status cresp;
    brpc::Controller ccntl;
    creq.set_path(path);
    creq.set_mode(static_cast<uint32_t>(mode));
    rpc_->mds()->CreateFile(&ccntl, &creq, &cresp, nullptr);
    if (ccntl.Failed()) return -ECOMM;
    auto ccode = StatusUtils::NormalizeCode(cresp.code());
    if (ccode != rpc::STATUS_SUCCESS) return -StatusToErrno(ccode);
    return Open(path, flags, out_fd);
}

int DfsClient::Read(int fd, char* buf, size_t size, off_t offset, ssize_t& out_bytes) {
    if (!rpc_ || !rpc_->srm()) return -ECOMM;
    auto it = fd_to_inode_.find(fd);
    if (it == fd_to_inode_.end()) return -EBADF;

    storagenode::ReadRequest req;
    storagenode::ReadReply resp;
    brpc::Controller cntl;
    req.set_node_id(cfg_.default_node_id);
    req.set_chunk_id(static_cast<uint64_t>(it->second));
    req.set_offset(static_cast<uint64_t>(offset));
    req.set_length(static_cast<uint64_t>(size));
    rpc_->srm()->Read(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) return -ECOMM;
    auto code = StatusUtils::NormalizeCode(resp.status().code());
    if (code != rpc::STATUS_SUCCESS) return -StatusToErrno(code);
    out_bytes = static_cast<ssize_t>(resp.bytes_read());
    if (out_bytes > 0 && static_cast<size_t>(out_bytes) <= size) {
        std::memcpy(buf, resp.data().data(), static_cast<size_t>(out_bytes));
    }
    return 0;
}

int DfsClient::Write(int fd, const char* buf, size_t size, off_t offset, ssize_t& out_bytes) {
    if (!rpc_ || !rpc_->srm()) return -ECOMM;
    auto it = fd_to_inode_.find(fd);
    if (it == fd_to_inode_.end()) return -EBADF;

    storagenode::WriteRequest req;
    storagenode::WriteReply resp;
    brpc::Controller cntl;
    req.set_node_id(cfg_.default_node_id);
    req.set_chunk_id(static_cast<uint64_t>(it->second));
    req.set_offset(static_cast<uint64_t>(offset));
    req.set_data(buf, size);
    req.set_checksum(0);
    req.set_flags(0);
    req.set_mode(0644);

    rpc_->srm()->Write(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) return -ECOMM;
    auto code = StatusUtils::NormalizeCode(resp.status().code());
    if (code != rpc::STATUS_SUCCESS) return -StatusToErrno(code);
    out_bytes = static_cast<ssize_t>(resp.bytes_written());
    return 0;
}

int DfsClient::Close(int fd) {
    auto it = fd_to_inode_.find(fd);
    if (it != fd_to_inode_.end()) {
        fd_to_inode_.erase(it);
    }
    return 0;
}
