#include "recommend.pb.h"
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <gflags/gflags.h>
#include <iostream>
#include <string>

DEFINE_string(server, "127.0.0.1:8001", "服务器地址 (ip:port)");
DEFINE_string(prompt, "你好,请介绍一下你自己", "Prompt to send");

int main(int argc, char* argv[]) {
    // 1. 解析命令行参数
    google::ParseCommandLineFlags(&argc, &argv, true);
    
    //注意：去掉了google::InitGoogleLogging, 依赖butil的自动初始化或默认行为

    // 2. 初始化channel
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = 10000; //10秒超时

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Fail to initialize channel to " << FLAGS_server << std::endl;
        return -1;
    }

    // 3. 准备stub
    recommend::RecommendService_Stub stub(&channel);

    // 4. 构造请求
    recommend::GenerateRequest request;
    request.set_prompt(FLAGS_prompt);
    request.set_max_tokens(50);
    request.set_temperature(0.7);
    request.set_top_p(0.9);
    request.set_user_id("cli_test_user");

    // 5. 构造响应
    recommend::GenerateResponse response;

    // 6. 控制器
    brpc::Controller cntl;

    std::cout << "Sending request to " << FLAGS_server << std::endl;

    // 7. 发起同步调用
    stub.Generate(&cntl, &request, &response, nullptr);
    
    if (cntl.Failed()) {
        std::cerr << "RPC failed: " << cntl.ErrorText() << std::endl;
        return -1;
    }

    // 8. 检查业务错误码
    if (response.error_code() != 0) {
        std::cerr << "Business error: " << response.error_message() << std::endl;
        return -1;
    }

    // 9. 打印结果
    std::cout << "=== Response ===" << std::endl;
    std::cout << "Text: " << response.generated_text() << std::endl;
    std::cout << "Reason: " << response.finish_reason() << std::endl;
    std::cout << "Tokens (Total/Prompt/Comp): " 
              << response.total_tokens() << "/"
              << response.prompt_tokens() << "/"
              << response.completion_tokens() << std::endl;  
    std::cout << "Latency: " << response.latency_ms() << " ms" << std::endl;

    return 0;
}
