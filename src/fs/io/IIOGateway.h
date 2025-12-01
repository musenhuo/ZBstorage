#pragma once
#include <vector>
#include "msg/IO.h"
// #include "srm/storage_manager/StorageResource.h"
// #include "../volume/StorageResourceGlobal.cpp"

class IIOGateway {
public:
    virtual ~IIOGateway() = default;
    virtual double processIO(const IORequest& req) = 0;               // <0 表失败
    virtual void processIOBatch(const std::vector<IORequest>& reqs) = 0;
};

// 使用示例-1
// class MyIOGateway : public IIOGateway {
// public:
//     double processIO(const IORequest& req) override {
//         // 处理单个IO请求
//         return 0.0;
//     }

//     void processIOBatch(const std::vector<IORequest>& reqs) override {
//         // 批量处理IO请求
//     }
// };

// 使用示例-2
// class RpcGateway final : public IIOGateway {
// public:
//     double processIO(const IORequest& req) override {
//         // 通过 RPC 发送单个请求并返回耗时（失败返回负值）
//         return rpc_client_.submit(req);
//     }

//     void processIOBatch(const std::vector<IORequest>& reqs) override {
//         rpc_client_.submit_batch(reqs);
//     }

// private:
//     RpcClient rpc_client_;
// };