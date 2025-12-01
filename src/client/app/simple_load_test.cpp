#include <gflags/gflags.h>
#include <brpc/channel.h>
#include <butil/logging.h>
#include <unistd.h>        // sleep / usleep
#include <cstdlib>         // rand, srand
#include <ctime>           // time

#include "posix.pb.h"      // 你的 proto 生成的头文件

DEFINE_string(server_addr, "127.0.0.1:8001", "Server address of PosixService");

// 只发送“成功”的 Stat 调用的路径
// 确保服务端能把这个 path 映射到一个真实存在的文件，比如 /tmp/my_dfs_storage/exist.txt
static const char* kSuccessPath = "/exist.txt";

bool CallStat(posix::PosixService_Stub* stub, const std::string& path) {
    posix::StatRequest req;
    posix::StatResponse res;
    brpc::Controller cntl;

    req.set_path(path);
    stub->Stat(&cntl, &req, &res, nullptr);  // 同步调用

    if (cntl.Failed()) {
        LOG(WARNING) << "RPC failed, path=" << path
                     << " error=" << cntl.ErrorText();
        return false;
    }

    if (res.error() != 0) {
        // 理论上这里不应该触发了，因为我们只发“成功”的路径
        LOG(WARNING) << "Stat(" << path << ") returned errno=" << res.error();
        return false;
    } else {
        LOG(INFO) << "Stat(" << path << ") success, size="
                  << res.stat_info().size();
        return true;
    }
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 初始化随机数种子
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // 初始化 brpc Channel
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.connection_type = "single";
    options.timeout_ms = 1000; // 1s 超时
    options.max_retry = 1;

    if (channel.Init(FLAGS_server_addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to init channel to " << FLAGS_server_addr;
        return -1;
    }

    posix::PosixService_Stub stub(&channel);

    LOG(INFO) << "Start sending Stat() requests to " << FLAGS_server_addr
              << " (random 0~2 requests per second)";

    while (true) {
        // 本秒要发送多少个请求：0, 1, 2 随机
        int num_requests = std::rand() % 3;

        for (int i = 0; i < num_requests; ++i) {
            CallStat(&stub, kSuccessPath);
        }

        // 每秒一轮
        sleep(1);
    }

    return 0;
}
