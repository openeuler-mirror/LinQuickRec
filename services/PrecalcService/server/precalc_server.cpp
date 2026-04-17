/**
 * @file precalc_server.cpp
 * @brief 前置计算服务服务端
 * 
 * 功能：
 * 1. 接收用户特征数据
 * 2. 生成前置计算结果（8.5MB，可配置）
 * 3. 使用 user_id+ 时间戳构造 key
 * 4. 使用元戎 Create+Set 接口写入前置计算结果到 KVWorker
 * 5. 生成 payload（随机乱码）用于模拟负载，补齐到 100KB
 * 6. 返回 key 和 payload
 */

#include "precalc.pb.h"
#include <brpc/server.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <butil/fast_rand.h>
#include <gflags/gflags.h>

#include <datasystem/kv_client.h>

#include <chrono>
#include <sstream>
#include <vector>
#include <random>
#include <cstring>
#include <memory>

using namespace datasystem;

// ============================================================================
// 配置参数
// ============================================================================

DEFINE_int32(server_port, 8004, "服务器监听端口");
DEFINE_string(kvworker_host, "127.0.0.1", "元戎 KVWorker 主机地址");
DEFINE_int32(kvworker_port, 31501, "元戎 KVWorker 端口");
DEFINE_double(precalc_result_size_mb, 8.5, "前置计算结果大小（MB），默认 8.5MB");
DEFINE_int32(ttl_seconds, 5, "TTL 时间（秒）");
DEFINE_int32(response_total_size_kb, 100, "响应总大小（key+payload），默认 100KB");
DEFINE_bool(enable_timing_stats, true, "是否启用详细时延统计");

// ============================================================================
// 工具函数
// ============================================================================

/**
 * @brief 生成当前时间的微秒级时间戳字符串
 * 
 * @return std::string 微秒级时间戳字符串
 */
std::string generate_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    return std::to_string(micros);
}

/**
 * @brief 从用户特征数据中提取 user_id
 * 
 * @param user_feat 用户特征数据
 * @return uint64_t 用户 ID
 */
uint64_t extract_user_id(const std::string& user_feat) {
    if (user_feat.size() >= sizeof(uint64_t)) {
        return *reinterpret_cast<const uint64_t*>(user_feat.data());
    }
    return butil::fast_rand();
}

/**
 * @brief 生成指定大小的前置计算结果（随机 tensor 数据）
 * 
 * @param size_mb 数据大小（MB）
 * @return std::string 生成的前置计算结果
 */
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

/**
 * @brief 生成指定大小的随机 payload 数据（乱码），用于模拟负载
 * 
 * @param size_kb 数据大小（KB）
 * @return std::string 生成的 payload 数据
 */
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

// ============================================================================
// 服务实现类
// ============================================================================

/**
 * @brief 前置计算服务实现类
 */
class PrecalcServiceImpl : public precalc::PrecalcService {
public:
    /**
     * @brief 构造函数
     */
    PrecalcServiceImpl() {
        LOG(INFO) << "PrecalcServiceImpl initialized";
        LOG(INFO) << "Precalc result size: " << FLAGS_precalc_result_size_mb << " MB";
        LOG(INFO) << "Response total size: " << FLAGS_response_total_size_kb << " KB (key + payload)";
        LOG(INFO) << "TTL: " << FLAGS_ttl_seconds << " seconds";
    }

    /**
     * @brief 处理前置计算请求
     * 
     * @param request 请求对象
     * @param response 响应对象
     * @param done 完成回调
     */
    void Precalculate(const precalc::PrecalcRequest* request,
                      precalc::PrecalcResponse* response,
                      google::protobuf::Closure* done) override {
        
        brpc::ClosureGuard done_guard(done);
        
        // 时间点 1: 服务端收到请求
        int64_t server_receive_us = butil::gettimeofday_us();
        
        LOG(INFO) << "Precalculate request received";
        
        if (request->user_feat().empty()) {
            LOG(ERROR) << "Empty user_feat in request";
            response->set_user_feat_key("");
            response->set_payload("");
            return;
        }
        
        // 生成 user_id + 时间戳作为 key
        uint64_t user_id = extract_user_id(request->user_feat());
        std::string timestamp = generate_timestamp();
        std::string user_feat_key = std::to_string(user_id) + "_" + timestamp;
        
        LOG(INFO) << "Generated user_feat_key: " << user_feat_key
                  << ", user_feat_size: " << request->user_feat().size() << " bytes";
        
        // 生成前置计算结果（8.5MB）
        std::string precalc_result = generate_precalc_result(FLAGS_precalc_result_size_mb);
        
        // 配置元戎 KV 客户端
        ConnectOptions connectOptions;
        connectOptions.host = FLAGS_kvworker_host;
        connectOptions.port = FLAGS_kvworker_port;
        
        KVClient kv_client(connectOptions);
        
        // 初始化连接
        Status status = kv_client.Init();
        if (!status.IsOk()) {
            LOG(ERROR) << "KVClient init failed: " << status.ToString();
            response->set_user_feat_key("");
            response->set_payload("");
            return;
        }
        
        // 配置 Set 参数
        SetParam param;
        param.ttlSecond = FLAGS_ttl_seconds;
        param.writeMode = WriteMode::NONE_L2_CACHE;
        param.existence = ExistenceOpt::NONE;
        param.cacheType = CacheType::MEMORY;
        
        // 时间点 2: 开始写入 KVWorker
        int64_t kvwrite_start_us = butil::gettimeofday_us();
        
        // 使用 Create+Set 接口减少内存拷贝
        std::shared_ptr<Buffer> buffer;
        status = kv_client.Create(user_feat_key, precalc_result.size(), param, buffer);
        if (!status.IsOk()) {
            LOG(ERROR) << "KVClient Create failed: " << status.ToString();
            response->set_user_feat_key("");
            response->set_payload("");
            return;
        }
        
        // 将前置计算结果拷贝到 buffer 中
        // Buffer 提供了 data() 方法获取指针
        std::memcpy(buffer->data(), precalc_result.data(), precalc_result.size());
        
        // 调用 Set 将数据缓存到数据系统
        status = kv_client.Set(buffer);
        if (!status.IsOk()) {
            LOG(ERROR) << "KVClient Set failed: " << status.ToString();
            response->set_user_feat_key("");
            response->set_payload("");
            return;
        }
        
        // 时间点 3: KVWorker 写入完成
        int64_t kvwrite_end_us = butil::gettimeofday_us();
        int64_t kvwrite_cost_us = kvwrite_end_us - kvwrite_start_us;
        
        LOG(INFO) << "Precalc result written to KVWorker: key=" << user_feat_key
                  << ", size=" << precalc_result.size() << " bytes (" 
                  << precalc_result.size() / (1024.0 * 1024.0) << " MB)";
        
        // 生成 payload 用于模拟负载，补齐到 100KB
        // payload 大小 = response_total_size_kb * 1024 - key.size()
        size_t total_response_bytes = FLAGS_response_total_size_kb * 1024;
        size_t key_size = user_feat_key.size();
        size_t payload_size = total_response_bytes - key_size;
        
        if (payload_size > 0) {
            std::string payload = generate_payload(payload_size / 1024);  // 转换为 KB
            
            // 时间点 4: 服务端发送响应
            int64_t server_send_us = butil::gettimeofday_us();
            
            // 设置响应
            response->set_user_feat_key(user_feat_key);
            response->set_payload(payload);
            
            // 计算时延
            int64_t server_process_us = server_send_us - server_receive_us;
            
            LOG(INFO) << "Precalculate success:"
                      << " key=" << user_feat_key
                      << ", key_size=" << key_size << " bytes"
                      << ", payload_size=" << payload.size() << " bytes"
                      << ", total_response_size=" << (key_size + payload.size()) << " bytes"
                      << ", precalc_result_size=" << precalc_result.size() << " bytes (" 
                      << precalc_result.size() / (1024.0 * 1024.0) << " MB)";
            
            // 打印时延统计
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

private:
    // 不再需要 kvworker_channel_，直接使用元戎 KVClient
};

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    logging::SetLoggingLevel(logging::BLOG_INFO);
    butil::AtExitManager exit_manager;
    
    PrecalcServiceImpl precalc_service;
    
    brpc::Server server;
    
    if (server.AddService(&precalc_service, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add PrecalcService";
        return -1;
    }
    
    brpc::ServerOptions server_options;
    server_options.num_threads = 128;
    
    if (server.Start(FLAGS_server_port, &server_options) != 0) {
        LOG(ERROR) << "Failed to start server on port " << FLAGS_server_port;
        return -1;
    }
    
    LOG(INFO) << "PrecalcService started on port " << FLAGS_server_port;
    LOG(INFO) << "Precalc result size: " << FLAGS_precalc_result_size_mb << " MB";
    LOG(INFO) << "Response total size: " << FLAGS_response_total_size_kb << " KB (key + payload)";
    LOG(INFO) << "TTL: " << FLAGS_ttl_seconds << " seconds";
    LOG(INFO) << "KVWorker address: " << FLAGS_kvworker_host << ":" << FLAGS_kvworker_port;
    
    server.RunUntilAskedToQuit();
    
    LOG(INFO) << "PrecalcService stopped";
    return 0;
}
