// monitor/monitor_service.hpp

#pragma once

#include "messaging_module.hpp"       // 提供了 IService 接口
#include "common/msg_protocol.hpp"    // 提供了 Command 枚举
#include "common/monitor_types.hpp"   // 提供了 FileInfo 结构体
#include <iostream>
#include <chrono>

// --- 关键修正：包含所有需要用到的内部类的完整定义 ---
#include "msg/zeromq/dispatcher.hpp"
#include "msg/zeromq/router_protocol.hpp"
#include "msg/zeromq/messenger.hpp"

// 注意：之前的 MdsService 现在统一为 MonitorService
class MonitorService : public IService {
public:
    void registerHandlersAndTypes(Dispatcher& dispatcher, RouterProtocol& protocol) override {
        // 现在编译器认识 protocol 的成员函数了
        protocol.register_type<std::string>(Command::GetFileInfoRequest);
        protocol.register_type<FileInfo>(Command::GetFileInfoResponse);
        
        // 现在编译器也认识 dispatcher 的成员函数和 Messenger 类型了
        dispatcher.register_handler(Command::GetFileInfoRequest, 
            [this](Message& req, Messenger& msgr){ this->handle_get_file_info(req, msgr); });
    }

private:
    // 现在编译器认识 Messenger 类型了
    void handle_get_file_info(Message& request, Messenger& msgr) {
        try {
            auto filename = std::any_cast<std::string>(request.payload);
            std::cout << "[MonitorService] Received request for file info: " << filename << std::endl;

            FileInfo file_info;
            file_info.filename = filename;
            if (filename == "hello.txt") {
                file_info.size_bytes = 1024;
                file_info.creation_timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            } else {
                file_info.size_bytes = 0;
            }

            Message reply;
            reply.identity = request.identity;
            reply.command = Command::GetFileInfoResponse;
            reply.payload = file_info;

            // 现在编译器也认识 msgr 的成员函数了
            msgr.send_message(std::move(reply));
            std::cout << "[MonitorService] Sent file info response for: " << filename << std::endl;

        } catch (const std::bad_any_cast& e) {
            std::cerr << "Error: Invalid payload for GetFileInfoRequest. Expected a string." << std::endl;
        }
    }
};