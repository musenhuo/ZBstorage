# 定义可执行文件
add_executable(my_app main.cpp)

# 只需要链接我们封装好的库
target_link_libraries(my_app PRIVATE dfs_client)

# 因为 C 程序链接了 C++ 库，添加 C++ 运行时
target_link_libraries(my_app PRIVATE ${CMAKE_CXX_STANDARD_LIBRARIES})

# --- [新添加] 编译 C++ 压力测试程序 ---
add_executable(simple_load_test simple_load_test.cpp)

# 链接依赖项
target_link_libraries(simple_load_test 
    PRIVATE 
        dfs_client                    # 链接我们的客户端库
        ${CMAKE_CXX_STANDARD_LIBRARIES} # 链接 C++ 运行时
)