#include <gflags/gflags.h>
#include <brpc/channel.h>
#include "echo.pb.h"

DEFINE_string(server, "0.0.0.0:8000", "IP Address and Port of server");

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // A Channel represents a communication line to a Server. Notice that
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;

    // Initialize the channel, NULL means using default options.
    brpc::ChannelOptions options;
    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // Normally, you should not call a Channel directly, but instead construct
    // a stub Service wrapping it. stub can be shared by all threads as well.
    echo::EchoService_Stub stub(&channel);

    // Send a request and wait for the response every 1 second.
    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        // We will receive response synchronously, safe to put variables
        // on stack.
        echo::EchoRequest request;
        echo::EchoResponse response;
        brpc::Controller cntl;

        request.set_message("hello world");

        cntl.set_log_id(log_id++);  // Set the log_id to track the request.

        // Because `done'(the last parameter) is NULL, this is a synchronous
        // RPC. We should not call cntl->Failed() before this RPC returns.
        stub.Echo(&cntl, &request, &response, NULL);

        if (!cntl.Failed()) {
            LOG(INFO) << "Received response from " << cntl.remote_side()
                      << ": " << response.message();
        } else {
            LOG(WARNING) << cntl.ErrorText();
        }
        sleep(1);
    }

    return 0;
}