// client_improved.cpp
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

// --- 改进点：端口号应与服务器一致 ---
#define PORT 5678
#define BUFFER_SIZE 1024

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    const char* hello = "Hello from client";
    
    // --- 改进点：正确的缓冲区声明 ---
    char buffer[BUFFER_SIZE] = {0};

    // 1. 创建套接字
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error"); // 使用 perror 可以打印更详细的错误信息
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 将IPv4地址从文本转换为二进制形式
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    // 2. 连接到服务器
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    // 3. 发送数据
    send(sock, hello, strlen(hello), 0);
    std::cout << "Hello message sent" << std::endl;

    // 4. 接收服务器的回声
    ssize_t valread = recv(sock, buffer, BUFFER_SIZE - 1, 0); // 读取 BUFFER_SIZE-1 以便可以手动添加\0
    if (valread > 0) {
        // --- 改进点：安全地处理和打印接收到的数据 ---
        // 方法一：使用 std::cout.write
        std::cout << "Received echo (" << valread << " bytes): ";
        std::cout.write(buffer, valread);
        std::cout << std::endl;

        // 方法二：手动添加空终止符（如果想作为字符串处理）
        // buffer[valread] = '\0';
        // std::cout << "Received echo as string: " << buffer << std::endl;

    } else if (valread == 0) {
        std::cout << "Server closed the connection." << std::endl;
    } else {
        perror("recv failed");
    }

    // 5. 关闭套接字
    close(sock);
    return 0;
}