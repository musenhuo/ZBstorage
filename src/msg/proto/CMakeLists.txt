# Protobuf 已由根 CMakeLists.txt 查找

# 生成 .pb.h 和 .pb.cc 文件
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS posix.proto)

# 定义库目标
add_library(dfs_proto STATIC ${PROTO_SRCS})

# 声明公共属性
target_include_directories(dfs_proto
    PUBLIC
        # 1. 包含生成的 .pb.h 文件
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(dfs_proto
    PUBLIC
        # 2. 链接由 pkg-config 找到的 Protobuf 库
        ${PROTOBUF_LIBRARIES}
)