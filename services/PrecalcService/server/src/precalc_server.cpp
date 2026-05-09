// 1. 对应的头文件
#include "precalc_server.h"

// 2. 标准库头文件
#include <chrono>
#include <sstream>
#include <vector>
#include <random>
#include <cstring>
#include <memory>
#include <string>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/server.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>
#include <datasystem/kv_client.h>

// 5. 本项目内其他头文件
#include "common/global_thread_pool.h"

DEFINE_int32(server_port, 8004, "服务器监听端口");
DEFINE_string(kvworker_host, "141.61.84.245", "元戎 KVWorker 主机地址");
DEFINE_int32(kvworker_port, 31502, "元戎 KVWorker 端口 (PrecalcService)");
DEFINE_string(etcd_address, "141.61.84.245:2379", "ETCD 地址");
DEFINE_double(precalc_result_size_mb, 8.5, "前置计算结果大小（MB），默认 8.5MB");
DEFINE_int32(ttl_seconds, 5, "TTL 时间（秒）");
DEFINE_int32(user_feat_key_size_kb, 100, "user_feat_key 大小（KB），默认 100KB");
DEFINE_bool(enable_timing_stats, true, "是否启用详细时延统计");
DEFINE_int32(payload_size_kb, 100, "payload 大小（KB），默认 100KB");

namespace precalc {

/**
 * @brief 生成指定大小的随机字符串（有效 UTF-8）
 * 
 * @param size_bytes 数据大小（字节）
 * @return std::string 生成的随机字符串（只包含 ASCII 可打印字符）
 */
std::string generate_random_string(size_t size_bytes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(32, 126);  // ASCII 可打印字符范围（空格到~）
    
    std::string result;
    result.resize(size_bytes);
    
    for (size_t i = 0; i < size_bytes; ++i) {
        result[i] = static_cast<char>(dis(gen));
    }
    
    return result;
}

std::string generate_precalc_result(double size_mb) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(32, 126);  // ASCII 可打印字符范围（空格到~）
    
    size_t total_bytes = static_cast<size_t>(size_mb * 1024 * 1024);
    std::string precalc_result;
    precalc_result.resize(total_bytes);
    
    for (size_t i = 0; i < total_bytes; ++i) {
        precalc_result[i] = static_cast<char>(dis(gen));
    }
    
    LOG(INFO) << "Generated precalc result with size: " << total_bytes << " bytes (" 
              << size_mb << " MB)";
    
    return precalc_result;
}

PrecalcServiceImpl::PrecalcServiceImpl() {
    LOG(INFO) << "PrecalcServiceImpl initialized";
    LOG(INFO) << "Precalc result size: " << FLAGS_precalc_result_size_mb << " MB";
    LOG(INFO) << "user_feat_key size: " << FLAGS_user_feat_key_size_kb << " KB";
    LOG(INFO) << "TTL: " << FLAGS_ttl_seconds << " seconds";
}

void PrecalcServiceImpl::Precalculate(google::protobuf::RpcController* controller,
                                      const PrecalcRequest* request,
                                      PrecalcResponse* response,
                                      google::protobuf::Closure* done) {
    
    brpc::ClosureGuard done_guard(done);
    (void)controller;  // 显式忽略未使用的参数，消除警告
    
    // 使用线程池异步处理请求
    auto& pool = common::get_global_thread_pool();
    
    // 提交任务到线程池
    auto future = pool.submit([this, request]() {
        // 创建响应对象
        PrecalcResponse local_response;
        
        // 处理请求
        process_precalc_request(request, &local_response);
        
        return local_response;
    });
    
    // 等待任务完成
    try {
        PrecalcResponse result = future.get();
        response->CopyFrom(result);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Thread pool task failed: " << e.what();
        response->set_user_feat_key("");
    }
}

void PrecalcServiceImpl::process_precalc_request(const PrecalcRequest* request,
                                                  PrecalcResponse* response) {
    
    int64_t server_receive_us = butil::gettimeofday_us();
    
    LOG(INFO) << "Precalculate request received";
    
    if (request->user_feat().empty()) {
        LOG(ERROR) << "Empty user_feat in request";
        response->set_user_feat_key("");
        response->set_payload("");
        return;
    }
    
    // 取 user_feat 的前 6 位作为 user_feat_key
    std::string user_feat_key;
    if (request->user_feat().size() >= 16) {
        user_feat_key = request->user_feat().substr(0, 16);
    } else {
        user_feat_key = request->user_feat();
    }
    
    LOG(INFO) << "Generated user_feat_key: " << user_feat_key
              << ", size: " << user_feat_key.size() << " bytes"
              << ", user_feat_size: " << request->user_feat().size() << " bytes";
    
    std::string precalc_result = generate_precalc_result(FLAGS_precalc_result_size_mb);
    
    ConnectOptions connectOptions;
    connectOptions.host = FLAGS_kvworker_host;
    connectOptions.port = FLAGS_kvworker_port;
    LOG(INFO) << "ConnectionOptions";

    KVClient kv_client(connectOptions);
    LOG(INFO) << "kv_client";

    Status status = kv_client.Init();
    if (!status.IsOk()) {
        LOG(ERROR) << "KVClient init failed: " << status.ToString();
        response->set_user_feat_key("");
        response->set_payload("");
        return;
    }
    LOG(INFO) << "KVClient init success";
    
    SetParam param;
    param.ttlSecond = FLAGS_ttl_seconds;
    param.writeMode = WriteMode::NONE_L2_CACHE;
    param.existence = ExistenceOpt::NONE;
    param.cacheType = CacheType::MEMORY;
    LOG(INFO) << "set param success";

    int64_t kvwrite_start_us = butil::gettimeofday_us();
    
    std::shared_ptr<Buffer> buffer;
    status = kv_client.Create(user_feat_key, precalc_result.size(), param, buffer);
    if (!status.IsOk()) {
        LOG(ERROR) << "KVClient Create failed: " << status.ToString();
        response->set_user_feat_key("");
        return;
    }
    LOG(INFO) << "KVClient Create success";
    
    std::memcpy(buffer->MutableData(), precalc_result.data(), precalc_result.size());
    
    status = kv_client.Set(buffer);
    if (!status.IsOk()) {
        LOG(ERROR) << "KVClient Set failed: " << status.ToString();
        response->set_user_feat_key("");
        return;
    }
    LOG(INFO) << "KVClient Set success";
    
    int64_t kvwrite_end_us = butil::gettimeofday_us();
    int64_t kvwrite_cost_us = kvwrite_end_us - kvwrite_start_us;
    
    LOG(INFO) << "Precalc result written to KVWorker: key=" << user_feat_key
              << ", size=" << precalc_result.size() << " bytes (" 
              << precalc_result.size() / (1024.0 * 1024.0) << " MB)";
    
    // 生成 payload
    std::string payload = generate_random_string(FLAGS_payload_size_kb * 1024);
    response->set_payload(payload);
    
    int64_t server_send_us = butil::gettimeofday_us();
    
    response->set_user_feat_key(user_feat_key);
    
    int64_t server_process_us = server_send_us - server_receive_us;
    
    LOG(INFO) << "Precalculate success:"
              << " key=" << user_feat_key
              << ", key_size=" << user_feat_key.size() << " bytes"
              << ", payload_size=" << payload.size() << " bytes (" 
              << payload.size() / 1024.0 << " KB)";
    
    if (FLAGS_enable_timing_stats) {
        LOG(INFO) << "Server timing breakdown:"
                  << " kvwrite_cost=" << kvwrite_cost_us / 1000.0 << " ms"
                  << " server_process_total=" << server_process_us / 1000.0 << " ms";
    }
    
    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;
    
    LOG(INFO) << "Precalculate completed, cost=" << cost_us / 1000.0 << " ms";
}

} // namespace precalc
