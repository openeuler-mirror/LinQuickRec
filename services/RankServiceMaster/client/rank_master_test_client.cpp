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
#include <memory>
#include <cstring>

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
DEFINE_int32(sku_count, 1000, "模拟的商品数量");
DEFINE_int32(payload_size_kb, 100, "payload 大小（KB），默认 100KB");
DEFINE_string(kvworker_host, "141.61.84.245", "元戎 KVWorker 主机地址");
DEFINE_int32(kvworker_port, 31502, "元戎 KVWorker 端口 (Rank)");
DEFINE_double(tensor_size_mb, 8.5, "tensor 大小（MB），默认 8.5MB");
DEFINE_int32(ttl_seconds, 5, "TTL 时间（秒），默认 5 秒");

std::string generate_numeric_key() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 9);
    
    std::string key;
    key.resize(16);
    for (int i = 0; i < 16; ++i) {
        key[i] = '0' + dis(gen);
    }
    return key;
}

std::string generate_skus_string(int count) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(100000, 999999);
    
    std::string result;
    for (int i = 0; i < count; ++i) {
        uint64_t sku_id = dis(gen);
        result += std::to_string(sku_id);
    }
    return result;
}

std::string generate_payload(size_t size_kb) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(32, 126);
    
    size_t total_bytes = size_kb * 1024;
    std::string payload;
    payload.resize(total_bytes);
    for (size_t i = 0; i < total_bytes; ++i) {
        payload[i] = static_cast<char>(dis(gen));
    }
    return payload;
}

std::string generate_tensor(double size_mb) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(32, 126);
    
    size_t total_bytes = static_cast<size_t>(size_mb * 1024 * 1024);
    std::string tensor;
    tensor.resize(total_bytes);
    for (size_t i = 0; i < total_bytes; ++i) {
        tensor[i] = static_cast<char>(dis(gen));
    }
    return tensor;
}

bool write_to_kvworker(const std::string& key, const std::string& value, 
                       const std::string& host, int port, int ttl) {
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
    param.ttlSecond = ttl;
    param.writeMode = WriteMode::NONE_L2_CACHE;
    param.existence = ExistenceOpt::NONE;
    param.cacheType = CacheType::MEMORY;
    
    std::shared_ptr<Buffer> buffer;
    status = kv_client.Create(key, value.size(), param, buffer);
    if (!status.IsOk()) {
        std::cerr << "KVClient Create failed: " << status.ToString() << std::endl;
        return false;
    }
    
    std::memcpy(buffer->MutableData(), value.data(), value.size());
    
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
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::cout << "========================================" << std::endl;
    std::cout << "RankMaster Service Client Test" << std::endl;
    std::cout << "========================================" << std::endl;

    std::string user_feat_key = generate_numeric_key();
    std::cout << "Generated user_feat_key: " << user_feat_key << std::endl;
    std::cout << "  size: " << user_feat_key.size() << " bytes" << std::endl;

    std::string tensor = generate_tensor(FLAGS_tensor_size_mb);
    std::cout << "Generated tensor: " << tensor.size() << " bytes (" 
              << FLAGS_tensor_size_mb << " MB)" << std::endl;

    std::cout << "\nWriting to KVWorker..." << std::endl;
    if (!write_to_kvworker(user_feat_key, tensor, FLAGS_kvworker_host, 
                          FLAGS_kvworker_port, FLAGS_ttl_seconds)) {
        std::cerr << "Failed to write to KVWorker" << std::endl;
        return -1;
    }

    std::string payload = generate_payload(FLAGS_payload_size_kb);
    std::cout << "Generated payload: " << payload.size() << " bytes (" 
              << FLAGS_payload_size_kb << " KB)" << std::endl;

    std::cout << "\nConnecting to RankMaster Server..." << std::endl;
    
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = FLAGS_timeout_ms;

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Fail to initialize channel to " << FLAGS_server << std::endl;
        return -1;
    }

    rank::RankMasterService_Stub stub(&channel);

    rank::RankMasterRequest request;
    request.set_user_feat_key(user_feat_key);
    request.set_skus(generate_skus_string(FLAGS_sku_count));
    request.set_payload(payload);

    std::cout << "\nRequest:" << std::endl;
    std::cout << "  user_feat_key: " << request.user_feat_key() << std::endl;
    std::cout << "  sku_count: " << FLAGS_sku_count << std::endl;
    std::cout << "  skus size: " << request.skus().size() << " bytes" << std::endl;
    std::cout << "  payload size: " << request.payload().size() << " bytes (" 
              << request.payload().size() / 1024.0 << " KB)" << std::endl;

    rank::RankMasterResponse response;
    brpc::Controller cntl;

    std::cout << "\nSending request to " << FLAGS_server << std::endl;

    int64_t client_send_us = butil::gettimeofday_us();

    stub.Rank(&cntl, &request, &response, nullptr);

    int64_t client_receive_us = butil::gettimeofday_us();
    int64_t total_latency_us = client_receive_us - client_send_us;

    if (cntl.Failed()) {
        std::cerr << "RPC failed: " << cntl.ErrorText() << std::endl;
        return -1;
    }

    LOG(INFO) << "Client timing breakdown:"
              << " total_latency=" << total_latency_us / 1000.0 << " ms"
              << " network_latency=" << cntl.latency_us() / 1000.0 << " ms";

    std::cout << "\n========================================" << std::endl;
    std::cout << "Response:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "candidates count: " << response.candidates_size() << std::endl;
    
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
