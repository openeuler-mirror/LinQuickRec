/**
 * @file precalc_test_client.cpp
 * @brief 前置计算服务客户端
 * 
 * 用于测试前置计算服务的功能
 */

// 1. 对应的头文件
#include "precalc.pb.h"

// 2. 标准库头文件
#include <iostream>
#include <string>
#include <vector>
#include <random>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

// 5. 本项目内其他头文件

DEFINE_string(server, "127.0.0.1:8004", "服务器地址 (ip:port)");
DEFINE_int32(user_feat_size_kb, 100, "用户特征数据大小（KB）");
DEFINE_double(precalc_result_size_mb, 8.5, "期望的前置计算结果大小（MB）");
DEFINE_int32(user_feat_key_size_kb, 100, "user_feat_key 大小（KB）");
DEFINE_int32(payload_size_kb, 100, "payload 大小（KB）");

/**
 * @brief 生成指定大小的随机数据（纯数字字符串）
 * 
 * @param size_kb 数据大小（KB）
 * @return std::string 生成的随机字符串（只包含数字 0-9）
 */
std::string generate_random_data(int size_kb) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis('0', '9');  // 数字字符范围（0-9）
    
    size_t total_bytes = static_cast<size_t>(size_kb) * 1024;
    std::string data;
    data.resize(total_bytes);
    
    for (size_t i = 0; i < total_bytes; ++i) {
        data[i] = static_cast<char>(dis(gen));
    }
    
    return data;
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    std::cout << "========================================" << std::endl;
    std::cout << "Precalc Service Client Test" << std::endl;
    std::cout << "========================================" << std::endl;

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = 30000;

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Fail to initialize channel to " << FLAGS_server << std::endl;
        return -1;
    }

    precalc::PrecalcService_Stub stub(&channel);

    precalc::PrecalcRequest request;
    std::string user_feat = generate_random_data(FLAGS_user_feat_size_kb);
    request.set_user_feat(user_feat);

    std::cout << "Request:" << std::endl;
    std::cout << "  user_feat size: " << request.user_feat().size() << " bytes (" 
              << FLAGS_user_feat_size_kb << " KB)" << std::endl;

    precalc::PrecalcResponse response;
    brpc::Controller cntl;

    std::cout << "\nSending request to " << FLAGS_server << std::endl;

    // 时间点 1: 客户端发送请求
    int64_t client_send_us = butil::gettimeofday_us();

    stub.Precalculate(&cntl, &request, &response, nullptr);

    // 时间点 2: 客户端收到响应
    int64_t client_receive_us = butil::gettimeofday_us();
    int64_t total_latency_us = client_receive_us - client_send_us;

    if (cntl.Failed()) {
        std::cerr << "RPC failed: " << cntl.ErrorText() << std::endl;
        return -1;
    }

    // 打印时延统计
    LOG(INFO) << "Client timing breakdown:"
              << " total_latency=" << total_latency_us / 1000.0 << " ms"
              << " network_latency=" << cntl.latency_us() / 1000.0 << " ms";

    std::cout << "\n========================================" << std::endl;
    std::cout << "Response:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "user_feat_key: " << response.user_feat_key() << std::endl;
    std::cout << "key size: " << response.user_feat_key().size() << " bytes" << std::endl;
    std::cout << "payload size: " << response.payload().size() << " bytes (" 
              << response.payload().size() / 1024.0 << " KB)" << std::endl;
    std::cout << "expected payload size: " << FLAGS_payload_size_kb << " KB" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Note: Precalc result (8.5 MB) is stored in KVWorker" << std::endl;
    std::cout << "Response contains user_feat_key (6 bytes) and payload" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Test completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
