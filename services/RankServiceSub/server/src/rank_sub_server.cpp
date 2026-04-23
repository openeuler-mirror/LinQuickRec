// 1. 对应的头文件
#include "rank_sub_server.h"

// 2. 标准库头文件
#include <chrono>
#include <sstream>
#include <vector>
#include <random>
#include <cstring>
#include <memory>
#include <string>
#include <algorithm>
#include <functional>
#include <thread>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/server.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>
#include <datasystem/kv_client.h>

// 5. 本项目内其他头文件
#include "global_thread_pool.h"

DEFINE_int32(server_port, 8006, "服务器监听端口");
DEFINE_string(kvworker_host, "141.61.84.245", "KVWorker 主机地址");
DEFINE_int32(kvworker_port, 31502, "KVWorker 端口 (Rank)");
DEFINE_string(etcd_address, "141.61.84.245:2379", "ETCD 地址");
DEFINE_int32(scoring_delay_ms, 100, "模拟打分耗时（毫秒）");
DEFINE_bool(enable_timing_stats, true, "是否启用详细时延统计");

namespace rank {

std::vector<uint64_t> parse_skus_from_string(const std::string& skus_sub) {
    std::vector<uint64_t> sku_ids;
    
    if (skus_sub.empty()) {
        LOG(WARNING) << "Empty skus_sub string";
        return sku_ids;
    }
    
    // 每 6 位数字是一个商品 ID
    const size_t SKU_ID_LENGTH = 6;
    size_t pos = 0;
    
    while (pos + SKU_ID_LENGTH <= skus_sub.size()) {
        std::string sku_str = skus_sub.substr(pos, SKU_ID_LENGTH);
        
        try {
            uint64_t sku_id = std::stoull(sku_str);
            sku_ids.push_back(sku_id);
        } catch (const std::exception& e) {
            LOG(WARNING) << "Failed to parse SKU ID: " << sku_str 
                        << ", error: " << e.what();
        }
        
        pos += SKU_ID_LENGTH;
    }
    
    LOG(INFO) << "Parsed " << sku_ids.size() << " SKU IDs from string";
    return sku_ids;
}

double simulate_score(uint64_t sku_id, const std::string& user_feat) {
    // 使用哈希函数生成模拟分数
    std::hash<std::string> hasher;
    size_t user_hash = hasher(user_feat);
    size_t sku_hash = std::hash<uint64_t>{}(sku_id);
    
    // 生成 0-100 之间的分数
    double score = static_cast<double>((user_hash ^ sku_hash) % 10000) / 100.0;
    
    return score;
}

RankSubServiceImpl::RankSubServiceImpl() {
    LOG(INFO) << "RankSubServiceImpl initialized";
    LOG(INFO) << "KVWorker address: " << FLAGS_kvworker_host 
              << ":" << FLAGS_kvworker_port;
    LOG(INFO) << "Scoring delay: " << FLAGS_scoring_delay_ms << " ms";
}

void RankSubServiceImpl::Rank(google::protobuf::RpcController* controller,
                              const RankSubRequest* request,
                              RankSubResponse* response,
                              google::protobuf::Closure* done) {
    
    // 使用线程池异步处理请求
    auto& pool = common::get_global_thread_pool();
    
    // 提交任务到线程池
    auto future = pool.submit([this, request]() {
        // 创建响应对象
        RankSubResponse local_response;
        
        // 处理请求
        process_rank_request(request, &local_response);
        
        return local_response;
    });
    
    // 等待任务完成
    try {
        RankSubResponse result = future.get();
        response->CopyFrom(result);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Thread pool task failed: " << e.what();
    }
    
    // 使用 ClosureGuard 确保 done 被正确调用
    brpc::ClosureGuard done_guard(done);
}

void RankSubServiceImpl::process_rank_request(const RankSubRequest* request,
                                              RankSubResponse* response) {
    
    int64_t server_receive_us = butil::gettimeofday_us();
    
    LOG(INFO) << "Rank request received";
    
    // 验证请求参数
    if (request->user_feat_key().empty()) {
        LOG(ERROR) << "Empty user_feat_key in request";
        return;
    }
    
    if (request->skus_sub().empty()) {
        LOG(ERROR) << "Empty skus_sub in request";
        return;
    }
    
    // 从 KVWorker 获取前置计算结果
    ConnectOptions connectOptions;
    connectOptions.host = FLAGS_kvworker_host;
    connectOptions.port = FLAGS_kvworker_port;
    
    KVClient kv_client(connectOptions);
    
    Status status = kv_client.Init();
    if (!status.IsOk()) {
        LOG(ERROR) << "KVClient init failed: " << status.ToString();
        return;
    }
    
    int64_t kv_read_start_us = butil::gettimeofday_us();
    
    // 使用 Buffer 方式获取数据（适合大数据）
    std::shared_ptr<Buffer> buffer;
    status = kv_client.Get(request->user_feat_key(), buffer);
    
    int64_t kv_read_end_us = butil::gettimeofday_us();
    int64_t kv_read_cost_us = kv_read_end_us - kv_read_start_us;
    
    if (!status.IsOk()) {
        LOG(ERROR) << "KVClient Get failed: " << status.ToString() 
                  << ", key: " << request->user_feat_key();
        return;
    }
    
    std::string user_feat(reinterpret_cast<char*>(buffer->data()), buffer->size());
    
    LOG(INFO) << "Retrieved user_feat from KVWorker: key=" 
              << request->user_feat_key() 
              << ", size=" << user_feat.size() << " bytes";
    
    // 解析商品 ID 列表
    std::vector<uint64_t> sku_ids = parse_skus_from_string(request->skus_sub());
    
    if (sku_ids.empty()) {
        LOG(ERROR) << "No SKU IDs parsed from skus_sub";
        return;
    }
    
    // 对每个商品进行打分
    int64_t scoring_start_us = butil::gettimeofday_us();
    
    // skus_score 格式：两个独立的数组 skus_id 和 skus_score
    // skus_score 存储分数 * 100 的整数值
    for (uint64_t sku_id : sku_ids) {
        double score = simulate_score(sku_id, user_feat);
        
        response->add_skus_id(sku_id);
        response->add_skus_score(static_cast<uint64_t>(score * 100));
    }
    
    int64_t scoring_end_us = butil::gettimeofday_us();
    int64_t scoring_cost_us = scoring_end_us - scoring_start_us;
    
    // 模拟计算耗时
    if (FLAGS_scoring_delay_ms > 0) {
        LOG(INFO) << "Simulating scoring delay: " << FLAGS_scoring_delay_ms << " ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_scoring_delay_ms));
    }
    
    int64_t server_send_us = butil::gettimeofday_us();
    int64_t server_process_us = server_send_us - server_receive_us;
    
    LOG(INFO) << "Rank processing completed:"
              << " sku_count=" << sku_ids.size()
              << ", response_skus_id_count=" << response->skus_id_size()
              << ", response_skus_score_count=" << response->skus_score_size();
    
    if (FLAGS_enable_timing_stats) {
        LOG(INFO) << "Server timing breakdown:"
                  << " kv_read_cost=" << kv_read_cost_us / 1000.0 << " ms"
                  << " scoring_cost=" << scoring_cost_us / 1000.0 << " ms"
                  << " simulated_delay=" << FLAGS_scoring_delay_ms << " ms"
                  << " server_process_total=" << server_process_us / 1000.0 << " ms";
    }
    
    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;
    
    LOG(INFO) << "Rank completed, cost=" << cost_us / 1000.0 << " ms";
}

} // namespace rank
