// user_service.hpp (新文件)
#pragma once

#include "dispatcher.hpp"
#include "router_protocol.hpp"
#include "common.hpp" // 需要 UserInfo 和 Message

// 定义一个服务基类，方便以后扩展其他服务
class IService {
public:
    virtual ~IService() = default;
    // 接口：让每个服务自己负责注册自己的类型和处理器
    virtual void registerHandlersAndTypes(Dispatcher& dispatcher, RouterProtocol& protocol) = 0;
};


// 专门处理用户相关命令的服务
class UserService : public IService {
public:
    // 实现接口，完成所有用户相关的注册工作
    void registerHandlersAndTypes(Dispatcher& dispatcher, RouterProtocol& protocol) override {
        // 1. 注册本服务需要用到的所有 payload 类型和它们对应的命令
        protocol.register_type<UserInfo>(Command::RegisterUser);
        protocol.register_type<std::string>(Command::RegisterReply);
        
        // 2. 注册本服务的消息处理器
        //    使用 lambda 表达式将成员函数绑定到 dispatcher
        dispatcher.register_handler(Command::RegisterUser, 
            [this](Message& req, Messenger& msgr){ this->handle_register_user(req, msgr); });
    }

private:
    // 将具体的业务逻辑实现为服务类的成员函数
    void handle_register_user(Message& request, Messenger& msgr) {
        try {
            // 从 std::any 中安全地取出 UserInfo 结构体
            auto user = std::any_cast<UserInfo>(request.payload);

            // 现在可以直接使用 user 对象了！
            std::cout << "[UserService] Received UserInfo: id=" << user.user_id 
                      << ", name=" << user.username 
                      << ", email=" << user.email << std::endl;
            
            // ... 可以在这里进行数据库插入等操作 ...
            std::cout << "[UserService] User '" << user.username << "' registered successfully." << std::endl;
            
            // 发送一个简单的文本回复
            Message reply;
            reply.identity = request.identity;
            reply.command = Command::RegisterReply;
            reply.payload = std::string("OK");
            msgr.send_message(std::move(reply));

        } catch (const std::bad_any_cast& e) {
            std::cerr << "Error: Payload for REGISTER_USER is not a UserInfo struct. " << e.what() << std::endl;
        }
    }
};