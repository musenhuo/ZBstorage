#include "messaging_module.hpp"

// 在实现文件中才包含所有内部头文件
#include <zmq.hpp>
#include "messenger.hpp"
#include "dispatcher.hpp"
#include "router_protocol.hpp"

// MsgServer::Implementation 结构体定义
// 这是 PIMPL 模式的核心，将所有私有成员都放在这里
struct MsgServer::Implementation {
    zmq::context_t context;
    std::unique_ptr<Messenger> messenger;
    std::shared_ptr<RouterProtocol> protocol;
    std::unique_ptr<Dispatcher> dispatcher;
    std::vector<std::unique_ptr<IService>> services;
    std::string bind_address;
    bool is_running = false;

    Implementation() : context(1) {} // 初始化 ZeroMQ context
};

// --- MsgServer 成员函数的实现 ---

MsgServer::MsgServer(const std::string& bind_address) 
    : m_impl(std::make_unique<Implementation>()) 
{
    m_impl->bind_address = bind_address;
}

MsgServer::~MsgServer() {
    if (m_impl->is_running) {
        stop();
    }
}

void MsgServer::register_service(std::unique_ptr<IService> service) {
    if (m_impl->is_running) {
        throw std::runtime_error("Cannot register service while server is running.");
    }
    m_impl->services.push_back(std::move(service));
}

void MsgServer::start() {
    if (m_impl->is_running) return;

    // 1. 初始化所有内部组件
    m_impl->messenger = std::make_unique<Messenger>(m_impl->context, zmq::socket_type::router);
    m_impl->messenger->bind(m_impl->bind_address);
    m_impl->protocol = std::make_shared<RouterProtocol>();
    m_impl->dispatcher = std::make_unique<Dispatcher>(std::move(m_impl->messenger), m_impl->protocol);

    // 2. 调用所有已注册服务的注册方法
    for (const auto& service : m_impl->services) {
        service->registerHandlersAndTypes(*m_impl->dispatcher, *m_impl->protocol);
    }
    
    // 3. 启动分发器
    m_impl->dispatcher->start();
    m_impl->is_running = true;
    std::cout << "MsgServer started on " << m_impl->bind_address << std::endl;
}

void MsgServer::run() {
    start();
    // 阻塞主线程，直到有外部信号或调用 stop()
    // 在实际应用中，这里可以用更优雅的方式阻塞，例如 std::condition_variable
    std::cout << "MsgServer is running. Press Enter to exit..." << std::endl;
    std::cin.get();
    stop();
}

void MsgServer::stop() {
    if (!m_impl->is_running) return;
    
    m_impl->dispatcher->stop();
    m_impl->is_running = false;
    std::cout << "MsgServer stopped." << std::endl;
}