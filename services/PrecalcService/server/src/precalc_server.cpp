#include "precalc_server.h"

#include <chrono>
#include <sstream>
#include <vector>
#include <random>
#include <cstring>
#include <memory>

#include "common/global_thread_pool.h"

DEFINE_int32(server_port, 8004, "服务器监听端口");
DEFINE_string(kvworker_host, "127.0.0.1", "元戎 KVWorker 主机地址");
DEFINE_int32(kvworker_port, 8006, "元戎 KVWorker 端口");
DEFINE_double(precalc_result_size_mb, 8.5, "前置计算结果大小（MB），默认 8.5MB");
DEFINE_int32(ttl_seconds, 5, "TTL 时间（秒）");
DEFINE_int32(response_total_size_kb, 100, "响应总大小（key+payload），默认 100KB");
DEFINE_bool(enable_timing_stats, true, "是否启用详细时延统计");

namespace precalc {

std::string generate_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    return std::to_string(micros);
}

uint64_t extract_user_id(const std::string& user_feat) {
    if (user_feat.size() >= sizeof(uint64_t)) {
        return *reinterpret_cast<const uint64_t*>(user_feat.data());
    }
    return butil::fast_rand();
}

std::string generate_precalc_result(double size_mb) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
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

std::string generate_payload(size_t size_kb) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    size_t total_bytes = size_kb * 1024;
    std::string payload;
    payload.resize(total_bytes);
    
    for (size_t i = 0; i < total_bytes; ++i) {
        payload[i] = static_cast<char>(dis(gen));
    }
    
    LOG(INFO) << "Generated payload with size: " << total_bytes << " bytes (" 
              << size_kb << " KB)";
    
    return payload;
}

PrecalcServiceImpl::PrecalcServiceImpl() {
    LOG(INFO) << "PrecalcServiceImpl initialized";
    LOG(INFO) << "Precalc result size: " << FLAGS_precalc_result_size_mb << " MB";
    LOG(INFO) << "Response total size: " << FLAGS_response_total_size_kb << " KB (key + payload)";
    LOG(INFO) << "TTL: " << FLAGS_ttl_seconds << " seconds";
}

void PrecalcServiceImpl::Precalculate(const PrecalcRequest* request,
                                      PrecalcResponse* response,
                                      google::protobuf::Closure* done) {
    
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
        response->set_payload("");
    }
    
    // 使用 ClosureGuard 确保 done 被正确调用
    brpc::ClosureGuard done_guard(done);
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
    
    uint64_t user_id = extract_user_id(request->user_feat());
    std::string timestamp = generate_timestamp();
    std::string user_feat_key = std::to_string(user_id) + "_" + timestamp;
    
    LOG(INFO) << "Generated user_feat_key: " << user_feat_key
              << ", user_feat_size: " << request->user_feat().size() << " bytes";
    
    std::string precalc_result = generate_precalc_result(FLAGS_precalc_result_size_mb);
    
    ConnectOptions connectOptions;
    connectOptions.host = FLAGS_kvworker_host;
    connectOptions.port = FLAGS_kvworker_port;
    
    KVClient kv_client(connectOptions);
    
    Status status = kv_client.Init();
    if (!status.IsOk()) {
        LOG(ERROR) << "KVClient init failed: " << status.ToString();
        response->set_user_feat_key("");
        response->set_payload("");
        return;
    }
    
    SetParam param;
    param.ttlSecond = FLAGS_ttl_seconds;
    param.writeMode = WriteMode::NONE_L2_CACHE;
    param.existence = ExistenceOpt::NONE;
    param.cacheType = CacheType::MEMORY;
    
    int64_t kvwrite_start_us = butil::gettimeofday_us();
    
    std::shared_ptr<Buffer> buffer;
    status = kv_client.Create(user_feat_key, precalc_result.size(), param, buffer);
    if (!status.IsOk()) {
        LOG(ERROR) << "KVClient Create failed: " << status.ToString();
        response->set_user_feat_key("");
        response->set_payload("");
        return;
    }
    
    std::memcpy(buffer->data(), precalc_result.data(), precalc_result.size());
    
    status = kv_client.Set(buffer);
    if (!status.IsOk()) {
        LOG(ERROR) << "KVClient Set failed: " << status.ToString();
        response->set_user_feat_key("");
        response->set_payload("");
        return;
    }
    
    int64_t kvwrite_end_us = butil::gettimeofday_us();
    int64_t kvwrite_cost_us = kvwrite_end_us - kvwrite_start_us;
    
    LOG(INFO) << "Precalc result written to KVWorker: key=" << user_feat_key
              << ", size=" << precalc_result.size() << " bytes (" 
              << precalc_result.size() / (1024.0 * 1024.0) << " MB)";
    
    size_t total_response_bytes = FLAGS_response_total_size_kb * 1024;
    size_t key_size = user_feat_key.size();
    size_t payload_size = total_response_bytes - key_size;
    
    if (payload_size > 0) {
        std::string payload = generate_payload(payload_size / 1024);
        
        int64_t server_send_us = butil::gettimeofday_us();
        
        response->set_user_feat_key(user_feat_key);
        response->set_payload(payload);
        
        int64_t server_process_us = server_send_us - server_receive_us;
        
        LOG(INFO) << "Precalculate success:"
                  << " key=" << user_feat_key
                  << ", key_size=" << key_size << " bytes"
                  << ", payload_size=" << payload.size() << " bytes"
                  << ", total_response_size=" << (key_size + payload.size()) << " bytes"
                  << ", precalc_result_size=" << precalc_result.size() << " bytes (" 
                  << precalc_result.size() / (1024.0 * 1024.0) << " MB)";
        
        if (FLAGS_enable_timing_stats) {
            LOG(INFO) << "Server timing breakdown:"
                      << " kvwrite_cost=" << kvwrite_cost_us / 1000.0 << " ms"
                      << " server_process_total=" << server_process_us / 1000.0 << " ms";
        }
    } else {
        LOG(ERROR) << "Payload size calculation error: key_size=" << key_size 
                   << ", total_response_bytes=" << total_response_bytes;
        response->set_user_feat_key("");
        response->set_payload("");
    }
    
    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;
    
    LOG(INFO) << "Precalculate completed, cost=" << cost_us / 1000.0 << " ms";
}

} // namespace precalc
