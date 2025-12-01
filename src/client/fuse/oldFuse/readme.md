如何编译和运行
环境准备:

安装 cmake, g++, protobuf, gflags, libfuse-dev (或 fuse-devel)。

编译并安装 bRPC：从 bRPC GitHub 克隆并按照官方文档编译。记下其安装路径。

修改 CMakeLists.txt 中的 /path/to/your/brpc/... 为你实际的 bRPC 路径。

编译:

Bash

mkdir build
cd build
cmake ..
make
运行:

启动 MDS 和 DS (在不同的终端)：

Bash

# 终端1
./mds_server
# 终端2
./ds_server
挂载 FUSE 客户端:

Bash

# 终端3
mkdir /tmp/mydistfs  # 创建挂载点
./fuse_client 127.0.0.1:8001 127.0.0.1:8002 /tmp/mydistfs -f 
# -f 参数让它在前台运行，方便调试
测试:

打开一个新的终端（终端4），你现在可以像操作普通目录一样操作 /tmp/mydistfs 了：

Bash

ls -l /tmp/mydistfs
mkdir /tmp/mydistfs/newdir
ls -l /tmp/mydistfs
echo "hello world" > /tmp/mydistfs/hello.txt
cat /tmp/mydistfs/hello.txt
你会在 fuse_client、mds_server 和 ds_server 的终端中看到相应的日志输出。

卸载:

在终端4中，执行 fusermount -u /tmp/mydistfs。

然后可以 Ctrl+C 停止 fuse_client 和两个服务器进程。