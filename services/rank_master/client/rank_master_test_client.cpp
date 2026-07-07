/**
 * @file rank_master_test_client.cpp
 * @brief 精排主图测试客户端
 *
 * 用于测试 RankMasterService 的功能，支持自定义输入内容
 */

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "common/logger.h"

#include <datasystem/kv_client.h>

#include "common/random_utils.h"
#include "discovery_provider.h"
#include "rank_master.pb.h"

DEFINE_string(server, "127.0.0.1:8005", "服务器地址 (ip:port)");
DEFINE_int32(timeout_ms, 30000, "超时时间（毫秒）");
DEFINE_int32(sku_count, 1000, "模拟的商品数量（当 --skus 为空时使用）");
DEFINE_string(registry_backend, "discovery_server", "Registry backend");
DEFINE_string(discovery_addr, "127.0.0.1:8100", "Discovery server address");
DEFINE_string(etcd_endpoints, "127.0.0.1:2379", "etcd endpoints");
DEFINE_string(kv_worker_service, "kv_worker", "KV Worker service name");
DEFINE_double(tensor_size_mb, 8.5, "tensor 大小（MB），默认 8.5MB");
DEFINE_int32(ttl_seconds, 5, "TTL 时间（秒），默认 5 秒");
DEFINE_string(user_feat_key, "", "自定义 user_feat_key（空值时随机生成 16 位数字）");
DEFINE_int32(payload_size_kb, 100, "payload 大小（KB），默认 100KB");

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

    // 处理 user_feat_key
    std::string user_feat_key;
    if (!FLAGS_user_feat_key.empty()) {
        user_feat_key = FLAGS_user_feat_key;
        std::cout << "Using custom user_feat_key: " << user_feat_key << std::endl;
    } else {
        user_feat_key = common::generate_random_numeric_string(16);
        std::cout << "Generated user_feat_key: " << user_feat_key << std::endl;
    }
    std::cout << "  size: " << user_feat_key.size() << " bytes" << std::endl;

    // 写入 KVWorker
    std::string tensor = common::generate_random_string(
        static_cast<size_t>(FLAGS_tensor_size_mb * 1024 * 1024));
    std::cout << "Generated tensor: " << tensor.size() << " bytes ("
              << FLAGS_tensor_size_mb << " MB)" << std::endl;

    std::cout << "\nWriting to KVWorker..." << std::endl;

    std::string kv_host;
    int kv_port = 0;
    {
        std::string addr = (FLAGS_registry_backend == "etcd")
            ? FLAGS_etcd_endpoints : FLAGS_discovery_addr;
        auto provider = discovery::CreateDiscoveryProvider(
            FLAGS_registry_backend, addr);
        auto instances = provider->Discover(FLAGS_kv_worker_service);
        if (instances.empty()) {
            std::cerr << "No kv_worker instances discovered" << std::endl;
            return -1;
        }
        kv_host = instances[0].host();
        kv_port = instances[0].port();
        std::cout << "Resolved kv_worker: " << kv_host << ":" << kv_port << std::endl;
    }
    if (!write_to_kvworker(user_feat_key, tensor, kv_host,
                          kv_port, FLAGS_ttl_seconds)) {
        std::cerr << "Failed to write to KVWorker" << std::endl;
        return -1;
    }

    std::cout << "\nConnecting to RankMaster Server..." << std::endl;

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.timeout_ms = FLAGS_timeout_ms;

    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "Fail to initialize channel to " << FLAGS_server << std::endl;
        return -1;
    }

    rank::RankMasterService_Stub stub(&channel);

    // 生成 SKU IDs
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(100000, 999999);

    rank::RankMasterRequest request;
    request.set_user_feat_key(user_feat_key);
    for (int i = 0; i < FLAGS_sku_count; ++i) {
        request.add_sku_ids(dis(gen));
    }
    if (FLAGS_payload_size_kb > 0) {
        request.set_payload(common::generate_random_string(FLAGS_payload_size_kb * 1024));
    }

    std::cout << "\nRequest:" << std::endl;
    std::cout << "  user_feat_key: " << request.user_feat_key() << std::endl;
    std::cout << "  sku_ids count: " << request.sku_ids_size() << std::endl;

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

    LOG_INFO << "Client timing breakdown:"
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
