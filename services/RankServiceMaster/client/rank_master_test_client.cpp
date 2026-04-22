/**
 * @file rank_master_test_client.cpp
 * @brief 精排主图测试客户端
 * 
 * 用于测试 RankMasterService 的功能
 */

// 1. 对应的头文件
#include "rank_master.pb.h"

// 2. 标准库头文件
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <random>
#include <cstring>
#include <memory>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>
#include <datasystem/kv_client.h>

// 5. 本项目内其他头文件

DEFINE_string(server, "127.0.0.1:8005", "服务器地址 (ip:port)");
DEFINE_int32(timeout_ms, 30000, "超时时间（毫秒）");
DEFINE_string(user_feat_key, "", "前置计算结果 key（可选，为空则自动生成）");
DEFINE_int32(user_feat_key_size_kb, 100, "user_feat_key 大小（KB），默认 100KB");
DEFINE_int32(sku_count, 1000, "模拟的商品数量");
DEFINE_int32(kvworker_port, 31502, "KVWorker 端口 (Rank)");
DEFINE_string(kvworker_host, "141.61.84.245", "KVWorker 主机地址");
DEFINE_string(etcd_address, "141.61.84.245:2379", "ETCD 地址");
DEFINE_double(tensor_size_mb, 8.5, "写入 KVWorker 的 tensor 大小（MB）");

/**
 * @brief 生成测试用的商品 ID 字符串
 * 
 * @param count 商品数量
 * @return std::string 商品 ID 字符串（每 6 位一个商品 ID）
 */
std::string generate_skus_string(int count) {
    std::string result;
    for (int i = 0; i < count; ++i) {
        // 生成 6 位数字的商品 ID
        uint64_t sku_id = 100000 + i;
        result += std::to_string(sku_id);
    }
    return result;
}

/**
 * @brief 生成 user_feat_key，格式为 user_id + 随机字符串
 * 
 * @param user_id 用户 ID
 * @param size_kb 总大小（KB）
 * @return std::string user_feat_key
 */
std::string generate_user_feat_key(uint64_t user_id, size_t size_kb) {
    std::string prefix = std::to_string(user_id) + "_";
    size_t random_length = size_kb * 1024 - prefix.size();
    
    if (random_length == 0) {
        return prefix;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis('a', 'z');
    
    std::string random_part;
    random_part.resize(random_length);
    for (size_t i = 0; i < random_length; ++i) {
        random_part[i] = static_cast<char>(dis(gen));
    }
    
    return prefix + random_part;
}

/**
 * @brief 生成指定大小的随机 tensor 数据
 * 
 * @param size_mb 数据大小（MB）
 * @return std::string 生成的 tensor 数据
 */
std::string generate_tensor_data(size_t size_mb) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    size_t total_bytes = size_mb * 1024 * 1024;
    std::string data;
    data.resize(total_bytes);
    
    for (size_t i = 0; i < total_bytes; ++i) {
        data[i] = static_cast<char>(dis(gen));
    }
    
    return data;
}

/**
 * @brief 写入数据到 KVWorker
 * 
 * @param key 键
 * @param value 值
 * @param host KVWorker 主机地址
 * @param port KVWorker 端口
 * @return true 写入成功
 * @return false 写入失败
 */
bool write_to_kvworker(const std::string& key, const std::string& value, 
                       const std::string& host, int port) {
    using namespace datasystem;
    
    ConnectOptions connectOptions;
    connectOptions.host = host;
    connectOptions.port = port;
    
    KVClient kv_client(connectOptions);
    Status status = kv_client.Init();
    if (!status.IsOk()) {
        std::cerr << "KVClient init failed: " << status.ToString() << std::endl;
        return false;
    }
    
    SetParam param;
    param.ttlSecond = 3600;  // 1 小时过期
    param.writeMode = WriteMode::NONE_L2_CACHE;
    param.existence = ExistenceOpt::NONE;
    param.cacheType = CacheType::MEMORY;
    
    std::shared_ptr<Buffer> buffer;
    status = kv_client.Create(key, value.size(), param, buffer);
    if (!status.IsOk()) {
        std::cerr << "KVClient Create failed: " << status.ToString() << std::endl;
        return false;
    }
    
    std::memcpy(buffer->data(), value.data(), value.size());
    
    status = kv_client.Set(buffer);
    if (!status.IsOk()) {
        std::cerr << "KVClient Set failed: " << status.ToString() << std::endl;
        return false;
    }
    
    std::cout << "Successfully wrote " << value.size() 
              << " bytes (" << value.size() / (1024.0 * 1024.0) << " MB)"
              << " to KVWorker with key: " << key << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    std::cout << "========================================" << std::endl;
    std::cout << "RankMaster Service Client Test" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. 生成 user_feat_key
    uint64_t user_id = 12345;
    std::string user_feat_key;
    
    if (FLAGS_user_feat_key.empty()) {
        user_feat_key = generate_user_feat_key(user_id, FLAGS_user_feat_key_size_kb);
        std::cout << "Generated user_feat_key: " << user_feat_key << std::endl;
        std::cout << "  size: " << user_feat_key.size() << " bytes (" 
                  << user_feat_key.size() / 1024.0 << " KB)" << std::endl;
    } else {
        user_feat_key = FLAGS_user_feat_key;
        std::cout << "Using provided user_feat_key: " << user_feat_key << std::endl;
    }
    
    // 2. 生成随机 tensor 数据（8.5MB）
    std::string tensor_data = generate_tensor_data(FLAGS_tensor_size_mb);
    std::cout << "Generated tensor data: " << tensor_data.size() << " bytes (" 
              << FLAGS_tensor_size_mb << " MB)" << std::endl;
    
    // 3. 写入 KVWorker
    std::cout << "\nWriting to KVWorker..." << std::endl;
    if (!write_to_kvworker(user_feat_key, tensor_data, FLAGS_kvworker_host, FLAGS_kvworker_port)) {
        std::cerr << "Failed to write to KVWorker" << std::endl;
        return -1;
    }
    
    // 4. 创建 Channel 并发送 Rank 请求
    std::cout << "\nConnecting to RankMaster Server..." << std::endl;
    
    // 创建 Channel
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = FLAGS_timeout_ms;

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Fail to initialize channel to " << FLAGS_server << std::endl;
        return -1;
    }

    rank::RankMasterService_Stub stub(&channel);

    // 构造请求
    rank::RankMasterRequest request;
    request.set_user_feat_key(user_feat_key);
    request.set_skus(generate_skus_string(FLAGS_sku_count));

    std::cout << "\nRequest:" << std::endl;
    std::cout << "  user_feat_key: " << request.user_feat_key() << std::endl;
    std::cout << "  user_feat_key size: " << request.user_feat_key().size() << " bytes (" 
              << request.user_feat_key().size() / 1024.0 << " KB)" << std::endl;
    std::cout << "  sku_count: " << FLAGS_sku_count << std::endl;
    std::cout << "  skus size: " << request.skus().size() << " bytes" << std::endl;

    rank::RankMasterResponse response;
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
    std::cout << "candidates count: " << response.candidates_size() << std::endl;
    
    // 打印前 20 个候选商品
    int print_count = std::min(20, response.candidates_size());
    if (print_count > 0) {
        std::cout << "Top " << print_count << " candidates:" << std::endl;
        for (int i = 0; i < print_count; ++i) {
            std::cout << "  [" << i << "] SKU: " << response.candidates(i) << std::endl;
        }
        
        if (response.candidates_size() > 20) {
            std::cout << "  ... and " << (response.candidates_size() - 20) 
                      << " more" << std::endl;
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Test completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
