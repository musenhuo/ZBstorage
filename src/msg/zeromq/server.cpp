// server.cpp (修改后的完整代码)
#include "dispatcher.hpp"
#include "router_protocol.hpp"
#include "user_service.hpp" // <--- 引入新的 UserService
#include <iostream>
#include <vector>
#include <memory>

// 全局的 handle_echo, handle_status 可以保留，或者也移入一个 "SystemService"
void handle_echo(Message& request, Messenger& msgr) {/*...*/}
void handle_status(Message& request, Messenger& msgr) {/*...*/}


int main() {
    zmq::context_t context(1);
    auto msgr = std::make_unique<Messenger>(context, zmq::socket_type::router);
    msgr->bind("tcp://*:5558");

    // 1. 初始化协议和分发器
    auto protocol = std::make_shared<RouterProtocol>();
    Dispatcher server_dispatcher(std::move(msgr), *protocol); // 注意这里 protocol 需要解引用

    // 2. 创建所有需要的服务模块
    std::vector<std::unique_ptr<IService>> services;
    services.push_back(std::make_unique<UserService>());
    // 如果未来有 FileService, AuthService 等，也在这里创建和添加
    // services.push_back(std::make_unique<FileService>());

    // 3. 让每个服务自己去注册它们的处理器和类型
    for (const auto& service : services) {
        service->registerHandlersAndTypes(server_dispatcher, *protocol);
    }
    
    // (可选) 注册不属于任何特定服务的通用处理器
    protocol->register_type<std::string>(Command::Echo);
    protocol->register_type<std::string>(Command::Status);
    server_dispatcher.register_handler(Command::Echo, handle_echo);
    server_dispatcher.register_handler(Command::Status, handle_status);


    // 4. 启动服务
    server_dispatcher.start();
    std::cout << "Dispatcher server started on tcp://*:5558" << std::endl;
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get(); // 等待用户输入回车后退出，比 sleep 更好

    // 优雅地停止
    server_dispatcher.stop();
    
    return 0;
}