#include "messenger.hpp"
#include "router_protocol.hpp" // 客户端也需要协议来发送消息
#include <iostream>
#include <thread>
#include <chrono>

// 注意：一个真正的客户端也需要一个循环和协议来处理收到的结构化回复
// 这里为了简化，只演示发送
void receiver_func(Messenger& msgr) {
    // 这个函数现在无法正确处理 std::any，仅作占位
    // 一个完整的实现需要客户端也有一个 dispatch 循环
}

int main() {
    zmq::context_t context(1);
    auto client_msgr = std::make_unique<Messenger>(context, zmq::socket_type::dealer);
    client_msgr->connect("tcp://localhost:5558");
    
    // --- 核心改动：客户端也需要 protocol 来序列化消息 ---
    auto protocol = std::make_shared<RouterProtocol>();
    protocol->register_type<UserInfo>(Command::RegisterUser);
    client_msgr->set_protocol(protocol);

    client_msgr->start();

    // --- 发送逻辑大大简化 ---
    std::cout << "--- Sending a UserInfo struct ---" << std::endl;
    
    UserInfo user;
    user.user_id = 1001;
    user.username = "Alice";
    user.email = "alice@example.com";
    std::cout << "Client sending UserInfo: id=" << user.user_id << ", name=" << user.username << std::endl;

    Message user_msg;
    user_msg.command = Command::RegisterUser;
    user_msg.payload = user; // 直接将结构体赋值给 std::any
    
    client_msgr->send_message(std::move(user_msg));

    std::cout << "Message sent. Client will exit." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1)); // 等待消息发送
    client_msgr->stop();

    return 0;
}