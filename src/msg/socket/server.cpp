// server_improved.cpp
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 5678
#define BUFFER_SIZE 1024

int main() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // 1. 创建套接字文件描述符
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 设置套接字选项，允许地址和端口重用
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 2. 绑定套接字到指定IP和端口
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 3. 监听端口
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port " << PORT << std::endl;

    // --- 改进点：使用循环来持续接受客户端连接 ---
    while (true) {
        int client_socket;
        std::cout << "\nWaiting for a new connection..." << std::endl;

        // 4. 接受客户端连接
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            // 这里可以选择继续循环或者退出，对于accept失败通常建议记录日志后继续
            continue; 
        }
        
        std::cout << "Connection accepted." << std::endl;

        // --- 改进点：正确的缓冲区声明 ---
        char buffer[BUFFER_SIZE] = {0};
        ssize_t valread;

        // 5. 接收和发送数据
        while ((valread = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            // --- 改进点：安全地打印接收到的数据 ---
            std::cout << "Received " << valread << " bytes: ";
            std::cout.write(buffer, valread);
            std::cout << std::endl;

            // 回声：将接收到的数据原样发回
            // 对于简单的回声服务，通常不需要复杂的send循环，但这是更健壮的做法
            send(client_socket, buffer, valread, 0);

            // 清空缓冲区不是必须的，因为recv会覆盖旧内容，且我们使用valread来控制范围
            // 但如果为了调试或某些逻辑需要，保留也无妨
            // memset(buffer, 0, BUFFER_SIZE); 
        }

        if (valread == 0) {
            std::cout << "Client disconnected." << std::endl;
        } else {
            perror("recv");
        }

        // 6. 关闭与当前客户端的套接字
        close(client_socket);
    }

    // 在实际应用中，服务器的监听套接字通常不会关闭，除非服务器要正常停机
    // 为了让这个示例能够退出（例如通过Ctrl+C），我们把关闭server_fd的操作放在循环外
    // 在这个无限循环的例子中，下面的代码实际上不会被执行
    close(server_fd);

    return 0;
}