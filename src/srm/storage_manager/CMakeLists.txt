cmake_minimum_required(VERSION 3.16)
project(SRM_MAIN LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 收集 srm 可执行所需的源码
set(SRM_MAIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/storage_manager/StorageResource.cpp

    # storagenode 相关实现
    ${CMAKE_CURRENT_SOURCE_DIR}/../storagenode/StoreageNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../storagenode/hard_disc/SSD.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../storagenode/hard_disc/HDD.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../storagenode/optical/OpticalDisc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../storagenode/optical/OpticalDiscLibrary.cpp

    # 卷与块管理实现（Volume 依赖 BlockManager）
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/volume/Volume.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/volume/StorageResourceGlobal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/block/BlockManager.cpp

    # 日志实现
    ${CMAKE_CURRENT_SOURCE_DIR}/../debug/ZBLog.cpp
)

add_executable(srm_main ${SRM_MAIN_SOURCES})

# 头文件搜索路径
target_include_directories(srm_main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}                # src/srm
    ${CMAKE_CURRENT_SOURCE_DIR}/..             # src（用于包含 msg/, fs/, storagenode/ 等）
    ${CMAKE_CURRENT_SOURCE_DIR}/../debug       # 使得 #include "ZBLog.h" 可被找到
    ${CMAKE_CURRENT_SOURCE_DIR}/../storagenode # 直接包含 storagenode 子目录头
    ${CMAKE_CURRENT_SOURCE_DIR}/../storagenode/hard_disc  # 使得 #include "SSD.h"/"HDD.h" 可被找到
    ${CMAKE_CURRENT_SOURCE_DIR}/../storagenode/optical    # 使得 #include "OpticalDisc*.h" 可被找到
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs          # 直接包含 fs 子目录头
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/block    # 使 Volume.h 能找到 BlockManager.h
)

# 如系统已安装 nlohmann_json，可选择启用（头-only，非必须）
# find_package(nlohmann_json QUIET)
# if(nlohmann_json_FOUND)
#     target_link_libraries(srm_main PRIVATE nlohmann_json::nlohmann_json)
# endif()
