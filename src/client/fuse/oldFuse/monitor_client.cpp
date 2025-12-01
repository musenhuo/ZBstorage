// client/mds_client.cpp (新文件)

#include "msg/zeromq/messenger.hpp"
#include "msg/zeromq/router_protocol.hpp"
#include "common/msg_protocol.hpp"
#include "common/monitor_types.hpp"  
#include <iostream>
#include <thread>

int main() {
    zmq::context_t context(1);
    auto messenger = std::make_unique<Messenger>(context, zmq::socket_type::dealer);
    messenger->connect("tcp://localhost:5560");
    
    auto protocol = std::make_shared<RouterProtocol>();
    // 客户端需要知道如何序列化请求 (string) 和反序列化回复 (FileInfo)
    protocol->register_type<std::string>(Command::GetFileInfoRequest);
    protoc-ol->register_type<FileInfo>(Command::GetFileInfoResponse);
    messenger->set_protocol(protocol);

    messenger->start();

    // 构建并发送请求
    Message request;
    request.command = Command::GetFileInfoRequest;
    request.payload = std::string("hello.txt");
    messenger->send_message(std::move(request));
    std::cout << "Client: Sent request for 'hello.txt'" << std::endl;

    // 等待并接收回复
    std::vector<zmq::message_t> parts;
    if (messenger->recv_raw_message(parts)) {
        auto opt_reply = protocol->parse_message(parts);
        if (opt_reply && opt_reply->command == Command::GetFileInfoResponse) {
            try {
                auto file_info = std::any_cast<FileInfo>(opt_reply->payload);
                std::cout << "Client: Received response:" << std::endl;
                std::cout << "  Filename: " << file_info.filename << std::endl;
                std::cout << "  Size: " << file_info.size_bytes << " bytes" << std::endl;
                std::cout << "  Timestamp: " << file_info.creation_timestamp << std::endl;
            } catch(const std::bad_any_cast& e) {
                std::cerr << "Client: Failed to cast reply payload." << std::endl;
            }
        }
    } else {
        std::cerr << "Client: Failed to receive reply from server." << std::endl;
    }

    messenger->stop();
    return 0;
}