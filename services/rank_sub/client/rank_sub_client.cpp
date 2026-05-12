/**
 * @file rank_sub_client.cpp
 * @brief 精排子图客户端
 * 
 * 用于与 RankSubServer 通信，发送打分请求
 */

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "rank_sub.pb.h"

DEFINE_string(server, "127.0.0.1:8006", "服务器地址 (ip:port)");
DEFINE_int32(timeout_ms, 10000, "超时时间（毫秒）");
DEFINE_string(user_feat_key, "", "前置计算结果 key");
DEFINE_string(skus_sub, "", "商品 ID 字符串（每 6 位一个商品 ID）");

/**
 * @brief 生成测试用的商品 ID 字符串
 * 
 * @param count 商品数量
 * @return std::string 商品 ID 字符串（每 6 位一个商品 ID）
 */
std::string generate_skus_sub_string(int count) {
    std::string result;
    for (int i = 0; i < count; ++i) {
        // 生成 6 位数字的商品 ID
        uint64_t sku_id = 100000 + i;
        result += std::to_string(sku_id);
    }
    return result;
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    std::cout << "========================================" << std::endl;
    std::cout << "RankSub Service Client Test" << std::endl;
    std::cout << "========================================" << std::endl;

    // 创建 Channel
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = FLAGS_timeout_ms;

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Fail to initialize channel to " << FLAGS_server << std::endl;
        return -1;
    }

    rank::RankSubService_Stub stub(&channel);

    // 构造请求
    rank::RankSubRequest request;
    
    if (FLAGS_user_feat_key.empty()) {
        // 使用默认测试 key
        request.set_user_feat_key("test_user_12345");
    } else {
        request.set_user_feat_key(FLAGS_user_feat_key);
    }
    
    if (FLAGS_skus_sub.empty()) {
        // 生成默认测试数据（10 个商品）
        request.set_skus_sub(generate_skus_sub_string(10));
    } else {
        request.set_skus_sub(FLAGS_skus_sub);
    }

    std::cout << "Request:" << std::endl;
    std::cout << "  user_feat_key: " << request.user_feat_key() << std::endl;
    std::cout << "  skus_sub size: " << request.skus_sub().size() << " bytes" << std::endl;

    rank::RankSubResponse response;
    brpc::Controller cntl;

    std::cout << "\nSending request to " << FLAGS_server << std::endl;

    // 记录发送时间
    int64_t client_send_us = butil::gettimeofday_us();

    // 发起同步调用
    stub.Rank(&cntl, &request, &response, nullptr);

    // 记录接收时间
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
    std::cout << "skus_id count: " << response.skus_id_size() << std::endl;
    std::cout << "skus_score count: " << response.skus_score_size() << std::endl;
    
    // 解析并打印结果（使用两个独立的数组）
    if (response.skus_id_size() == response.skus_score_size() && response.skus_id_size() > 0) {
        std::cout << "SKU Scores:" << std::endl;
        for (int i = 0; i < response.skus_id_size(); ++i) {
            uint64_t sku_id = response.skus_id(i);
            uint64_t score_int = response.skus_score(i);
            double score = static_cast<double>(score_int) / 100.0;
            std::cout << "  SKU: " << sku_id << ", Score: " << score << std::endl;
        }
    } else {
        std::cout << "Invalid response format (skus_id and skus_score counts should match)" << std::endl;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Test completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
