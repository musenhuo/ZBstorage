cmake_minimum_required(VERSION 3.10)
project(zb_rpc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

# brpc assumed installed system-wide
find_library(BRPC_LIB brpc)
if(NOT BRPC_LIB)
  message(FATAL_ERROR "brpc library not found. Please install brpc or set BRPC_LIB.")
endif()

set(PROTO_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/rpc_common.proto
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/mds.proto
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/storage.proto
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/vfs.proto
)

set(PROTOBUF_IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/proto
  ${CMAKE_CURRENT_SOURCE_DIR}/server
  ${CMAKE_CURRENT_SOURCE_DIR}/client
  ${PROJECT_SOURCE_DIR}/../src
  ${PROJECT_SOURCE_DIR}/../src/storagenode
  ${PROJECT_SOURCE_DIR}/../src/storagenode/hard_disc
  ${PROJECT_SOURCE_DIR}/../src/fs
  ${PROJECT_SOURCE_DIR}/../src/mds
  ${PROJECT_SOURCE_DIR}/../src/debug
  ${PROJECT_SOURCE_DIR}/../src/srm
  ${PROJECT_SOURCE_DIR}/../third_party
)

set(COMMON_SRCS
  ${PROJECT_SOURCE_DIR}/../src/storagenode/optical/OpticalDiscLibrary.cpp
  ${PROJECT_SOURCE_DIR}/../src/storagenode/optical/OpticalDisc.cpp
  ${PROJECT_SOURCE_DIR}/../src/storagenode/StoreageNode.cpp
  ${PROJECT_SOURCE_DIR}/../src/storagenode/hard_disc/SSD.cpp
  ${PROJECT_SOURCE_DIR}/../src/storagenode/hard_disc/HDD.cpp
  ${PROJECT_SOURCE_DIR}/../src/fs/block/BlockManager.cpp
  ${PROJECT_SOURCE_DIR}/../src/fs/volume/Volume.cpp
  ${PROJECT_SOURCE_DIR}/../src/fs/volume/StorageResourceGlobal.cpp
  ${PROJECT_SOURCE_DIR}/../src/fs/volume/VolumeManager.cpp
  ${PROJECT_SOURCE_DIR}/../src/fs/volume/VolumeRegistry.cpp
  ${PROJECT_SOURCE_DIR}/../src/fs/VFS/VFS_new.cpp
  ${PROJECT_SOURCE_DIR}/../src/fs/io/LocalStorageGateway.cpp
  ${PROJECT_SOURCE_DIR}/../src/srm/storage_manager/StorageResource.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/server/Server.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/server/DirectoryLockTable.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/server/DirStore.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/allocator/VolumeAllocator.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/metadataserver/MetadataManager.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/metadataserver/KVStore.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/collector/collector.cpp
  ${PROJECT_SOURCE_DIR}/../src/srm/image_manager/ImageManager.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/inode/inode.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/inode/InodeStorage.cpp
  ${PROJECT_SOURCE_DIR}/../src/mds/inode/InodeTimestamp.cpp
  ${PROJECT_SOURCE_DIR}/../src/debug/ZBLog.cpp
)

add_library(rpc_proto ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(rpc_proto PUBLIC ${Protobuf_LIBRARIES})

add_executable(mds_rpc_server
  server/mds_service_impl.cpp
  ${COMMON_SRCS}
)
target_link_libraries(mds_rpc_server PRIVATE rpc_proto ${Protobuf_LIBRARIES} ${BRPC_LIB})

add_executable(storage_rpc_server
  server/storage_service_impl.cpp
  ${COMMON_SRCS}
)
target_link_libraries(storage_rpc_server PRIVATE rpc_proto ${Protobuf_LIBRARIES} ${BRPC_LIB})

add_executable(vfs_rpc_server
  server/vfs_service_impl.cpp
  ${COMMON_SRCS}
)
target_link_libraries(vfs_rpc_server PRIVATE rpc_proto ${Protobuf_LIBRARIES} ${BRPC_LIB})

add_executable(rpc_client
  client/VFS_dist.cpp
  ${COMMON_SRCS}
)
target_link_libraries(rpc_client PRIVATE rpc_proto ${Protobuf_LIBRARIES} ${BRPC_LIB})
