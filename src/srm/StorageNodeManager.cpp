#include "StorageNodeManager.h"

#include <brpc/channel.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <utility>

#include "msg/RPC/proto/rpc_common.pb.h"
#include "common/StatusUtils.h"

StorageNodeManager::StorageNodeManager(std::chrono::milliseconds heartbeat_timeout,
                                       std::chrono::milliseconds health_check_interval)
    : heartbeat_timeout_(heartbeat_timeout),
      health_check_interval_(health_check_interval) {}

StorageNodeManager::~StorageNodeManager() {
    Stop();
}

void StorageNodeManager::Start() {
    if (running_.exchange(true)) {
        return;
    }
    health_thread_ = std::thread([this]() { HealthLoop(); });
}

void StorageNodeManager::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (health_thread_.joinable()) {
        health_thread_.join();
    }
}

void StorageNodeManager::HandleRegister(const storagenode::RegisterRequest* request,
                                        storagenode::RegisterResponse* response) {
    if (!request || !response) {
        return;
    }
    if (request->ip().empty() || request->port() == 0) {
        StatusUtils::SetStatus(response->mutable_status(), rpc::STATUS_INVALID_ARGUMENT, "missing ip/port");
        return;
    }

    NodeContext ctx;
    ctx.node_id = GenerateNodeId();
    ctx.ip = request->ip();
    ctx.port = request->port();
    ctx.hostname = request->hostname();
    ctx.disks.assign(request->disks().begin(), request->disks().end());
    ctx.type = NodeType::Real;
    ctx.state = NodeState::Online;
    ctx.last_heartbeat = std::chrono::steady_clock::now();

    registry_.Upsert(std::move(ctx));
    response->set_node_id(ctx.node_id);
    StatusUtils::SetStatus(response->mutable_status(), rpc::STATUS_SUCCESS, "");
    std::cerr << "[SRM] node registered id=" << response->node_id() << " ip=" << request->ip()
              << ":" << request->port() << " disks=" << request->disks_size() << std::endl;
}

void StorageNodeManager::HandleHeartbeat(const storagenode::HeartbeatRequest* request,
                                         storagenode::HeartbeatResponse* response) {
    if (!request || !response) {
        return;
    }
    if (request->node_id().empty()) {
        StatusUtils::SetStatus(response->mutable_status(), rpc::STATUS_INVALID_ARGUMENT, "empty node_id");
        response->set_require_rereg(true);
        return;
    }
    bool ok = registry_.UpdateHeartbeat(request->node_id(),
                                        std::chrono::steady_clock::now());
    if (!ok) {
        StatusUtils::SetStatus(response->mutable_status(), rpc::STATUS_NODE_NOT_FOUND, "node not registered");
        response->set_require_rereg(true);
        return;
    }
    StatusUtils::SetStatus(response->mutable_status(), rpc::STATUS_SUCCESS, "");
    response->set_require_rereg(false);
}

void StorageNodeManager::HealthLoop() {
    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        auto snapshot = registry_.Snapshot();
        for (const auto& ctx : snapshot) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx.last_heartbeat);
            if (elapsed > heartbeat_timeout_ && ctx.state != NodeState::Offline) {
                registry_.MarkOffline(ctx.node_id);
                std::cerr << "[SRM] node offline id=" << ctx.node_id
                          << " elapsed_ms=" << elapsed.count() << std::endl;
            }
        }
        std::this_thread::sleep_for(health_check_interval_);
    }
}

std::string StorageNodeManager::GenerateNodeId() {
    uint64_t seq = id_seq_.fetch_add(1);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "node-" + std::to_string(now) + "-" + std::to_string(seq);
}

bool StorageNodeManager::GetNode(const std::string& node_id, NodeContext& ctx) const {
    return registry_.Get(node_id, ctx);
}

void StorageNodeManager::AddVirtualNode(const std::string& node_id, const SimulationParams& params) {
    NodeContext ctx;
    ctx.node_id = node_id;
    ctx.type = NodeType::Virtual;
    ctx.sim_params = params;
    ctx.state = NodeState::Online;
    ctx.last_heartbeat = std::chrono::steady_clock::now();
    registry_.Upsert(std::move(ctx));
}
