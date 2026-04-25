#include "proxy.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <gflags/gflags.h>

DEFINE_string(server, "127.0.0.1:8080", "Proxy 服务地址");
DEFINE_uint64(user_id, 12345, "用户 ID");
DEFINE_string(payload, "test_request", "附加数据");
DEFINE_int32(timeout_ms, 30000, "请求超时 (ms)");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    LOG(INFO) << "Gateway Test Client starting...";
    LOG(INFO) << "Connecting to Proxy at: " << FLAGS_server;

    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.timeout_ms = FLAGS_timeout_ms;
    opts.protocol = "http";
    opts.connection_type = "pooled";

    if (channel.Init(FLAGS_server.c_str(), &opts) != 0) {
        LOG(ERROR) << "Failed to connect to " << FLAGS_server;
        return -1;
    }

    proxy::Proxy_Stub stub(&channel);

    proxy::RecommendRequest request;
    request.set_user_id(FLAGS_user_id);
    request.set_payload(FLAGS_payload);

    LOG(INFO) << "Sending Recommend request:"
              << " user_id=" << FLAGS_user_id
              << " payload=" << FLAGS_payload;

    proxy::RecommendResponse response;
    brpc::Controller cntl;

    butil::Timer timer;
    timer.start();

    stub.Recommend(&cntl, &request, &response, nullptr);

    timer.stop();

    if (cntl.Failed()) {
        LOG(ERROR) << "Recommend RPC failed: " << cntl.ErrorText();
        return -1;
    }

    LOG(INFO) << "Recommend response received:"
              << " candidates=" << response.candidates_size()
              << " latency=" << timer.m_elapsed() << "ms";

    for (int i = 0; i < response.candidates_size(); ++i) {
        LOG(INFO) << "  candidate[" << i << "] = " << response.candidates(i);
        if (i >= 20) {
            LOG(INFO) << "  ... (" << (response.candidates_size() - 20) << " more)";
            break;
        }
    }

    LOG(INFO) << "Gateway Test Client finished successfully";
    return 0;
}
