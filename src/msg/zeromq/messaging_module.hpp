#pragma once

#include <string>
#include <memory>
#include <vector>

// 1. 包含最核心、外部必须知道的数据结构和枚举
#include "common/msg_protocol.hpp"
// #include "msg_protocol.hpp"

// ------------------- Forward Declarations -------------------
// 向前声明内部类，避免在头文件中暴露它们的完整定义
class Dispatcher;
class RouterProtocol;
class IProtocol;
// -------------------------------------------------------------


// 2. 定义服务接口（外部开发者需要继承这个类来实现自己的服务）
class IService {
public:
    virtual ~IService() = default;
    
    // 纯虚函数，强制每个服务实现自己的类型和处理器注册逻辑
    virtual void registerHandlersAndTypes(Dispatcher& dispatcher, RouterProtocol& protocol) = 0;
};


// 3. 定义核心的服务器外观类 (Facade)
class MsgServer {
public:
    // 构造函数：用户只需提供要监听的地址
    explicit MsgServer(const std::string& bind_address);
    ~MsgServer();

    // 禁止拷贝和移动，确保服务器实例唯一
    MsgServer(const MsgServer&) = delete;
    MsgServer& operator=(const MsgServer&) = delete;
    MsgServer(MsgServer&&) = delete;
    MsgServer& operator=(MsgServer&&) = delete;

    // 核心API：注册一个服务模块
    void register_service(std::unique_ptr<IService> service);

    // 启动服务器并阻塞，直到调用 stop()
    void run();
    
    // (可选) 异步启动服务器
    void start();

    // 停止服务器
    void stop();

private:
    // 使用 PIMPL (Pointer to Implementation) 模式隐藏所有内部成员
    // 这样用户就完全不需要知道 zmq::context_t, Messenger 等内部细节
    struct Implementation;
    std::unique_ptr<Implementation> m_impl;
};