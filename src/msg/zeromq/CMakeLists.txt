add_library(msg STATIC
    messaging_module.cpp 
)

# --- 关键修正：将 msg 模块自己的目录也声明为 PUBLIC ---
# 这样，链接到 msg 的其他模块（如 client）就能找到 messenger.hpp 等文件
target_include_directories(msg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(msg PUBLIC
    common
    ${PC_ZMQ_LIBRARIES}
    Threads::Threads
)