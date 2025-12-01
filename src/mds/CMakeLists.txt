cmake_minimum_required(VERSION 3.16)
project(ZBStorage_MDS CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1) MDS 自身源码
set(MDS_SERVER_SRCS
    server/Server.cpp
    server/DirStore.cpp
    server/DirectoryLockTable.cpp
)

# 2) 自动包含 mds/inode 与 mds/namespace 下实现源码（确保包含 inode.cpp）
file(GLOB MDS_INODE_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/inode/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/inode/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/inode/*.cxx"
)
file(GLOB MDS_NAMESPACE_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/namespace/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/namespace/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/namespace/*.cxx"
)
file(GLOB MDS_METADATASERVER_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/metadataserver/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/metadataserver/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/metadataserver/*.cxx"
)
list(APPEND MDS_SERVER_SRCS ${MDS_INODE_SRCS} ${MDS_NAMESPACE_SRCS} ${MDS_METADATASERVER_SRCS})

# Exclude demo binaries from the mds_server static library so we can build a separate demo target.
list(REMOVE_ITEM MDS_SERVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/metadataserver/demo_kv.cpp")

# Volume allocator implementation (VolumeManager linkage not required for this unit test build)
list(APPEND MDS_SERVER_SRCS
    stubs/VolumeAllocatorStub.cpp
    stubs/VolumeManagerStub.cpp)

add_library(mds_server STATIC ${MDS_SERVER_SRCS})

# 3) 头文件搜索路径
target_include_directories(mds_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..   # 使得 "mds/../" -> src
    ${CMAKE_CURRENT_SOURCE_DIR}      # 使得 "server/..."、"inode/..." 生效
)
target_compile_definitions(mds_server PRIVATE ZB_MDS_VOLUME_MANAGER_STUB)

# 4) 单测
add_executable(mds_server_ut MdsServer_test.cpp)
target_link_libraries(mds_server_ut mds_server)

add_executable(mds_server_stress MdsServer_stress.cpp)
target_link_libraries(mds_server_stress mds_server)

# MetadataManager focused unit test
add_executable(metadataserver_ut metadataserver/MetadataManager_test.cpp)
target_link_libraries(metadataserver_ut mds_server)

add_executable(metadataserver_bulk_ut metadataserver/MetadataManager_bulk_test.cpp)
target_link_libraries(metadataserver_bulk_ut mds_server)

# 如旧版 libstdc++ 需要 <filesystem>：
# target_link_libraries(mds_server_ut stdc++fs)

# Optional demo target: path->inode KV demo using KVStore in metadataserver.
# This target links the mds_server static library (which contains inode implementation and KVStore.cpp).
find_path(ROCKSDB_INCLUDE_DIR rocksdb/db.h)
find_library(ROCKSDB_LIB rocksdb)

if(ROCKSDB_INCLUDE_DIR AND ROCKSDB_LIB)
    message(STATUS "RocksDB found: include=${ROCKSDB_INCLUDE_DIR}, lib=${ROCKSDB_LIB}")
    add_executable(demo_kv metadataserver/demo_kv.cpp)
    target_include_directories(demo_kv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${ROCKSDB_INCLUDE_DIR})
    target_compile_definitions(demo_kv PRIVATE USE_ROCKSDB)
    target_link_libraries(demo_kv PRIVATE mds_server ${ROCKSDB_LIB})
else()
    message(STATUS "RocksDB not found, building demo_kv without RocksDB (file-backed).")
    add_executable(demo_kv metadataserver/demo_kv.cpp)
    target_include_directories(demo_kv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(demo_kv PRIVATE mds_server)
endif()