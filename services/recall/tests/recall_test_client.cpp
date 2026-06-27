/**
 * @file recall_test_client.cpp
 * @brief 召回服务客户端
 *
 * 用于测试召回服务的功能，支持自定义输入内容
 */

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "common/random_utils.h"
#include "recall.pb.h"

DEFINE_string(server, "127.0.0.1:8001", "服务器地址 (ip:port)");
DEFINE_uint64(user_id, 12345, "用户 ID");
DEFINE_int32(log_count, 3, "用户日志数量（当 --user_logs 为空时使用）");
DEFINE_string(user_logs, "", "用户日志（格式: \"1,2,3;4,5,6\"，分号分隔多个 log，逗号分隔 vec，空值时自动生成）");
DEFINE_int32(payload_size_kb, 100, "payload 负载大小（KB），默认 100KB");
DEFINE_int32(timeout_ms, 100000, "超时时间（毫秒），默认 100000ms");
/**
 * @brief 解析 user_logs 字符串
 * 格式: "1,2,3;4,5,6" -> 两个 log，vec 分别为 [1,2,3] 和 [4,5,6]
 */
std::vector<std::vector<uint32_t>> parse_user_logs(const std::string& user_logs_str) {
    std::vector<std::vector<uint32_t>> logs;

    if (user_logs_str.empty()) {
        return logs;
    }

    std::stringstream ss(user_logs_str);
    std::string log_str;

    while (std::getline(ss, log_str, ';')) {
        std::vector<uint32_t> vec;
        std::stringstream vec_ss(log_str);
        std::string val_str;

        while (std::getline(vec_ss, val_str, ',')) {
            val_str.erase(0, val_str.find_first_not_of(" \t"));
            val_str.erase(val_str.find_last_not_of(" \t") + 1);
            if (!val_str.empty()) {
                try {
                    vec.push_back(static_cast<uint32_t>(std::stoul(val_str)));
                } catch (const std::exception& e) {
                    std::cerr << "Warning: failed to parse vec value: " << val_str << std::endl;
                }
            }
        }

        if (!vec.empty()) {
            logs.push_back(vec);
        }
    }

    return logs;
}

/**
 * @brief 生成默认的用户日志
 */
std::vector<std::vector<uint32_t>> generate_default_logs(int count) {
    std::vector<std::vector<uint32_t>> logs;
    for (int i = 0; i < count; ++i) {
        std::vector<uint32_t> vec;
        for (int j = 0; j < 5; ++j) {
            vec.push_back(static_cast<uint32_t>(i * 10 + j));
        }
        logs.push_back(vec);
    }
    return logs;
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    std::cout << "========================================" << std::endl;
    std::cout << "Recall Service Client Test" << std::endl;
    std::cout << "========================================" << std::endl;

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = FLAGS_timeout_ms;

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Fail to initialize channel to " << FLAGS_server << std::endl;
        return -1;
    }

    recall::RecallService_Stub stub(&channel);

    recall::RecallRequest request;
    request.set_user_id(FLAGS_user_id);

    std::vector<std::vector<uint32_t>> logs;
    if (!FLAGS_user_logs.empty()) {
        logs = parse_user_logs(FLAGS_user_logs);
        std::cout << "Using custom user_logs: " << FLAGS_user_logs << std::endl;
    } else {
        logs = generate_default_logs(FLAGS_log_count);
        std::cout << "Using default user_logs (log_count=" << FLAGS_log_count << ")" << std::endl;
    }

    for (const auto& log_vec : logs) {
        recall::KRUserLog* log = request.add_user_logs();
        for (uint32_t v : log_vec) {
            log->add_vec(v);
        }
    }

    std::string payload = common::generate_random_string(FLAGS_payload_size_kb * 1024);
    request.set_payload(payload);

    std::cout << "Request:" << std::endl;
    std::cout << "  user_id: " << request.user_id() << std::endl;
    std::cout << "  user_logs count: " << request.user_logs_size() << std::endl;
    std::cout << "  payload size: " << request.payload().size() << " bytes (" << FLAGS_payload_size_kb << " KB)" << std::endl;

    recall::RecallResponse response;
    brpc::Controller cntl;

    std::cout << "\nSending request to " << FLAGS_server << std::endl;

    stub.Recall(&cntl, &request, &response, nullptr);

    if (cntl.Failed()) {
        std::cerr << "RPC failed: " << cntl.ErrorText() << std::endl;
        return -1;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "Response:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "SKU IDs count: " << response.sku_ids_size() << std::endl;

    if (response.sku_ids_size() > 0) {
        int print_count = std::min(20, response.sku_ids_size());
        std::cout << "First " << print_count << " SKU IDs: ";
        for (int i = 0; i < print_count; ++i) {
            std::cout << response.sku_ids(i);
            if (i < print_count - 1) {
                std::cout << ", ";
            }
        }
        std::cout << std::endl;

        if (response.sku_ids_size() > 20) {
            std::cout << "  ... and " << (response.sku_ids_size() - 20) << " more" << std::endl;
        }
    } else {
        std::cout << "No SKU IDs returned" << std::endl;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Test completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
