# 定义可执行文件
add_executable(posix_server server.cpp)

# 链接依赖
target_link_libraries(posix_server
    PRIVATE
        dfs_proto         # 链接内部的 proto 库
        ${BRPC_LIBRARIES} # 链接 bRPC 及其所有依赖
)