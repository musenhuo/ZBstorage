# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(DistFS_Client CXX)

set(CMAKE_CXX_STANDARD 17)

# --- 1. 将独立的协议模块包含进来 ---
# 我们假设 proto_definitions 目录与当前项目在同一级目录下
# CMake 会首先执行 ../proto_definitions/CMakeLists.txt
# 并创建一个名为 "proto_posix" 的库目标
add_subdirectory(../msg/proto ${CMAKE_CURRENT_BINARY_DIR}/proto_build)


# --- 2. 查找其他依赖项 ---
find_package(GFlags REQUIRED)
# find_package(FUSE REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse3) # 或者是 fuse, 取决于你的系统版本

pkg_check_modules(BRPC REQUIRED brpc)


# --- 3. Protobuf 代码生成部分已被移除 ---
# 因为协议模块已经帮我们处理了 .proto 文件的编译
# protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS distfs.proto)  <-- 这行被删除了


# --- 4. FUSE 客户端 ---
# 我们不再需要将 ${PROTO_SRCS} 添加到源文件列表中
# 因为协议库会自动处理链接
add_executable(fuse_client fuse_client.cpp)

# 链接库时，将原来的 ${PROTOBUF_LIBRARIES} 和 brpc 手动链接
# 替换为链接 bRPC 目标 (brpc::brpc) 和我们自己的协议目标 (proto_posix)
target_link_libraries(fuse_client
    PRIVATE
    ${FUSE_LIBRARIES}
    brpc::brpc           # bRPC 的现代化目标，包含了它自身和所有依赖
    posix          # 链接我们自己定义的协议库
)


# # --- 5. 服务器 ---
# add_executable(mds_server mds_server.cc)
# target_link_libraries(mds_server
#     PRIVATE
#     brpc::brpc
#     posix
# )

# add_executable(ds_server ds_server.cc)
# target_link_libraries(ds_server
#     PRIVATE
#     brpc::brpc
#     posix
# )