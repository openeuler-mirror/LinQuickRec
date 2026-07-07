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
#include "common/logger.h"
#include <butil/time.h>
#include <gflags/gflags.h>

#include "rank_sub.pb.h"
#include "common/random_utils.h"

DEFINE_string(server, "127.0.0.1:8006", "服务器地址 (ip:port)");
DEFINE_int32(timeout_ms, 10000, "超时时间（毫秒）");
DEFINE_string(user_feat, "", "用户特征数据");
DEFINE_int32(sku_count, 10, "生成测试 SKU 数量");
DEFINE_int32(payload_size_kb, 0, "请求 payload 大小 (KB)");

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
    
    if (FLAGS_user_feat.empty()) {
        request.set_user_feat("test_user_12345");
    } else {
        request.set_user_feat(FLAGS_user_feat);
    }
    
    int count = FLAGS_sku_count;
    for (int i = 0; i < count; ++i) {
        request.add_sku_ids(100000 + i);
    }

    if (FLAGS_payload_size_kb > 0) {
        request.set_payload(common::generate_random_string(FLAGS_payload_size_kb * 1024));
    }

    std::cout << "Request:" << std::endl;
    std::cout << "  user_feat: " << request.user_feat() << std::endl;
    std::cout << "  sku_ids count: " << request.sku_ids_size() << std::endl;
    std::cout << "  payload size: " << request.payload().size() << " bytes" << std::endl;

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
    LOG_INFO << "Client timing breakdown:"
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
