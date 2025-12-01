#include <gflags/gflags.h>
#include <brpc/server.h>
#include "echo.pb.h"

DEFINE_int32(port, 8000, "TCP Port of this server");

namespace echo {

class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {};
    virtual ~EchoServiceImpl() {};

    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const EchoRequest* request,
                      EchoResponse* response,
                      google::protobuf::Closure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        // Log the request.
        LOG(INFO) << "Received request[log_id=" << cntl->log_id()
                  << "] from " << cntl->remote_side()
                  << ": " << request->message();

        // Set the response.
        response->set_message(request->message());
    }
};

}  // namespace echo

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // Generally this is optional, but for some RPC protocols (like rtmp),
    // server must be started after AddService().
    brpc::Server server;

    // Instance of your service.
    echo::EchoServiceImpl echo_service_impl;

    // Add the service into server. Notice the second parameter, because the
    // service is created on stack, we don't want server to delete it, otherwise
    // use SERVER_OWNS_SERVICE.
    if (server.AddService(&echo_service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // Start the server.
    brpc::ServerOptions options;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}