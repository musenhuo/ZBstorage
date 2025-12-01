# bRPC 已由根 CMakeLists.txt 查找

# 定义库目标
add_library(dfs_client STATIC dfs_client.cpp)

# 声明公共属性
target_include_directories(dfs_client
    PUBLIC
        # 1. 包含 dfs_client.h
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(dfs_client
    PUBLIC
        # 2. 链接内部的 dfs_proto 目标
        dfs_proto
        # 3. 链接由 pkg-config 找到的 bRPC 及其所有依赖
        ${BRPC_LIBRARIES}
)