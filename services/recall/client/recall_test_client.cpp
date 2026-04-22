/**
 * @file brpc_client.cpp
 * @brief 召回服务客户端
 * 
 * 用于测试召回服务的功能
 */

// 1. 对应的头文件
#include "recall.pb.h"

// 2. 标准库头文件
#include <iostream>
#include <string>
#include <vector>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

// 5. 本项目内其他头文件

DEFINE_string(server, "127.0.0.1:8001", "服务器地址 (ip:port)");
DEFINE_uint64(user_id, 12345, "用户 ID");
DEFINE_int32(log_count, 3, "用户日志数量");

int main(int argc, char* argv[]) {
    // 1. 解析命令行参数
    google::ParseCommandLineFlags(&argc, &argv, true);

    std::cout << "========================================" << std::endl;
    std::cout << "Recall Service Client Test" << std::endl;
    std::cout << "========================================" << std::endl;

    // 2. 初始化 channel
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = 10000; // 10 秒超时

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Fail to initialize channel to " << FLAGS_server << std::endl;
        return -1;
    }

    // 3. 准备 stub
    recall::RecallService_Stub stub(&channel);

    // 4. 构造请求
    recall::RecallRequest request;
    request.set_user_id(FLAGS_user_id);
    request.set_other("test_request");

    // 添加用户日志（模拟数据）
    for (int i = 0; i < FLAGS_log_count; ++i) {
        recall::KRUserLog* log = request.add_user_logs();
        // 添加一些模拟的 vec 数据
        for (int j = 0; j < 5; ++j) {
            log->add_vec(i * 10 + j);
        }
    }

    std::cout << "Request:" << std::endl;
    std::cout << "  user_id: " << request.user_id() << std::endl;
    std::cout << "  user_logs count: " << request.user_logs_size() << std::endl;
    std::cout << "  other: " << request.other() << std::endl;

    // 5. 构造响应
    recall::RecallResponse response;

    // 6. 控制器
    brpc::Controller cntl;

    std::cout << "\nSending request to " << FLAGS_server << std::endl;

    // 7. 发起同步调用
    stub.Recall(&cntl, &request, &response, nullptr);

    if (cntl.Failed()) {
        std::cerr << "RPC failed: " << cntl.ErrorText() << std::endl;
        return -1;
    }

    // 8. 检查结果
    std::cout << "\n========================================" << std::endl;
    std::cout << "Response:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "SKU IDs count: " << response.sku_ids_size() << std::endl;
    
    if (response.sku_ids_size() > 0) {
        std::cout << "SKU IDs: ";
        for (int i = 0; i < response.sku_ids_size(); ++i) {
            std::cout << response.sku_ids(i);
            if (i < response.sku_ids_size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << std::endl;
    } else {
        std::cout << "No SKU IDs returned" << std::endl;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Test completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
