/**
 * @file feature_test_client.cpp
 * @brief 特征服务测试客户端
 *
 * 用于测试特征服务的功能，支持 GetUserFeatures 和 GetSKUFeatures 两个 RPC
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

#include "common/logger.h"
#include "feature.pb.h"

DEFINE_string(server, "127.0.0.1:8001", "Feature 服务地址 (ip:port)");
DEFINE_int32(timeout_ms, 30000, "RPC 超时时间（毫秒）");
DEFINE_uint64(user_id, 12345, "测试用户 ID");
DEFINE_string(sku_ids, "100,200,300", "测试 SKU ID 列表（逗号分隔）");

static std::vector<uint64_t> parse_sku_ids(const std::string& str) {
    std::vector<uint64_t> ids;
    std::string token;
    for (size_t i = 0; i <= str.size(); ++i) {
        if (i == str.size() || str[i] == ',') {
            if (!token.empty()) {
                ids.push_back(static_cast<uint64_t>(std::stoull(token)));
                token.clear();
            }
        } else {
            token += str[i];
        }
    }
    return ids;
}

static int test_user_features(feature::FeatureService_Stub& stub) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 1: GetUserFeatures" << std::endl;
    std::cout << "========================================" << std::endl;

    feature::UserFeatureRequest request;
    request.set_feature_type(feature::KuaiRand);
    auto* kr_req = request.mutable_kr_feat_req();
    kr_req->set_user_id(FLAGS_user_id);

    std::cout << "Request: user_id=" << FLAGS_user_id << std::endl;

    feature::UserFeatureResponse response;
    brpc::Controller cntl;

    int64_t send_us = butil::gettimeofday_us();
    stub.GetUserFeatures(&cntl, &request, &response, nullptr);
    int64_t recv_us = butil::gettimeofday_us();

    if (cntl.Failed()) {
        std::cerr << "GetUserFeatures RPC failed: " << cntl.ErrorText() << std::endl;
        return -1;
    }

    int64_t total_ms = (recv_us - send_us) / 1000;
    double network_ms = cntl.latency_us() / 1000.0;

    const auto& kr_rsp = response.kr_feat_rsp();
    std::cout << "Response:" << std::endl;
    std::cout << "  feature_type: " << response.feature_type() << std::endl;
    std::cout << "  user_logs count: " << kr_rsp.user_logs_size() << std::endl;
    for (int i = 0; i < kr_rsp.user_logs_size(); ++i) {
        std::cout << "    log[" << i << "] vec_size="
                  << kr_rsp.user_logs(i).vec_size() << std::endl;
    }
    std::cout << "  payload size: " << kr_rsp.payload().size() << " bytes" << std::endl;
    std::cout << "Latency: total=" << total_ms << " ms, network="
              << network_ms << " ms" << std::endl;
    std::cout << "PASSED" << std::endl;
    return 0;
}

static int test_sku_features(feature::FeatureService_Stub& stub,
                             const std::vector<uint64_t>& sku_ids) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 2: GetSKUFeatures" << std::endl;
    std::cout << "========================================" << std::endl;

    feature::SKUFeatureRequest request;
    request.set_feature_type(feature::KuaiRand);
    for (uint64_t id : sku_ids) {
        request.add_sku_ids(id);
    }

    std::cout << "Request: sku_count=" << sku_ids.size() << std::endl;

    feature::SKUFeatureResponse response;
    brpc::Controller cntl;

    int64_t send_us = butil::gettimeofday_us();
    stub.GetSKUFeatures(&cntl, &request, &response, nullptr);
    int64_t recv_us = butil::gettimeofday_us();

    if (cntl.Failed()) {
        std::cerr << "GetSKUFeatures RPC failed: " << cntl.ErrorText() << std::endl;
        return -1;
    }

    int64_t total_ms = (recv_us - send_us) / 1000;
    double network_ms = cntl.latency_us() / 1000.0;

    std::cout << "Response:" << std::endl;
    std::cout << "  feature_type: " << response.feature_type() << std::endl;
    std::cout << "  sku_feats count: " << response.kr_sku_feats_size() << std::endl;
    for (int i = 0; i < response.kr_sku_feats_size(); ++i) {
        const auto& feat = response.kr_sku_feats(i);
        std::cout << "    sku_id=" << feat.sku_id()
                  << " feat_length=" << feat.feat().size() << std::endl;
    }
    std::cout << "Latency: total=" << total_ms << " ms, network="
              << network_ms << " ms" << std::endl;
    std::cout << "PASSED" << std::endl;
    return 0;
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    std::cout << "========================================" << std::endl;
    std::cout << "Feature Service Client Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Server: " << FLAGS_server << std::endl;
    std::cout << "Timeout: " << FLAGS_timeout_ms << " ms" << std::endl;

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = FLAGS_timeout_ms;

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Failed to connect to " << FLAGS_server << std::endl;
        return -1;
    }

    feature::FeatureService_Stub stub(&channel);

    auto sku_ids = parse_sku_ids(FLAGS_sku_ids);
    std::cout << "User ID: " << FLAGS_user_id << std::endl;
    std::cout << "SKU IDs: " << FLAGS_sku_ids
              << " (count=" << sku_ids.size() << ")" << std::endl;

    int rc = 0;
    rc |= test_user_features(stub);
    rc |= test_sku_features(stub, sku_ids);

    std::cout << "\n========================================" << std::endl;
    if (rc == 0) {
        std::cout << "All tests passed!" << std::endl;
    } else {
        std::cout << "Some tests failed!" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return rc;
}
