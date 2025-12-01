cmake_minimum_required(VERSION 3.10)
project(dfs_fuse_client CXX)

# --- 查找 FUSE ---
# 使用我们之前确认可用的 pkg-config 方式
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse) # (或 fuse3)

# 添加 FUSE 的头文件路径
include_directories(${FUSE_INCLUDE_DIRS})

# 定义可执行文件
add_executable(dfs_fuse_client fuse_main.cpp)

# 为 dfs_fuse_client 目标添加 _FILE_OFFSET_BITS=64 编译定义
target_compile_definitions(dfs_fuse_client PRIVATE 
    _FILE_OFFSET_BITS=64
    FUSE_DEBUG 
)

# 链接依赖
target_link_libraries(dfs_fuse_client
    PRIVATE
        dfs_client          # 1. 链接我们封装的 bRPC 库
        ${FUSE_LIBRARIES}   # 2. 链接 FUSE 库
)

# 再次添加 C++ 运行时，因为 FUSE 库可能是 C 写的
target_link_libraries(dfs_fuse_client PRIVATE ${CMAKE_CXX_STANDARD_LIBRARIES})