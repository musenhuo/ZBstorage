
#include "messaging_module.hpp"
#include "monitor_service.hpp" // 确保包含了正确的头文件

int main(int argc, char* argv[]) {
    MsgServer server("tcp://*:5560");

    // --- 关键修正：使用新的类名 MonitorService ---
    server.register_service(std::make_unique<MonitorService>());

    server.run();
    return 0;
}