cmake_minimum_required(VERSION 3.0)
project(EchoExample)

# 1. 找到 PkgConfig 工具
# 这是使用 pkg-config 的第一步
find_package(PkgConfig REQUIRED)

# 2. 使用 PkgConfig 查找 bRPC
# 这会执行 `pkg-config --cflags brpc` 和 `pkg-config --libs brpc`
# 并将结果存入 BRPC_INCLUDE_DIRS 和 BRPC_LIBRARIES 等变量中
pkg_check_modules(BRPC REQUIRED brpc)

# 3. 找到 Protobuf (这部分保持不变)
find_package(Protobuf REQUIRED)

# 4. 添加头文件目录
# 将 Protobuf 和 bRPC 的头文件路径都添加进去
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${BRPC_INCLUDE_DIRS})

# 5. 生成 protobuf 文件 (这部分保持不变)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS echo.proto)

# 添加生成的头文件所在的目录
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# 6. 添加可执行文件 (这部分保持不变)
add_executable(echo_server server.cpp ${PROTO_SRCS})
add_executable(echo_client client.cpp ${PROTO_SRCS})

# 7. 链接库 (这是主要修改点)
# 将原来的 `brpc` 目标替换为 pkg-config 找到的 ${BRPC_LIBRARIES} 变量
# 这个变量包含了链接 bRPC 及其所有依赖项所需的所有 -l -L 参数
target_link_libraries(echo_server PRIVATE ${BRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})
target_link_libraries(echo_client PRIVATE ${BRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})