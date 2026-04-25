// 1. 对应的头文件
#include "rank_master_server.h"

// 2. 标准库头文件
#include <chrono>
#include <sstream>
#include <vector>
#include <random>
#include <cstring>
#include <memory>
#include <algorithm>
#include <functional>
#include <future>
#include <map>

// 3. 系统库头文件

// 4. 其他库头文件
#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

// 5. 本项目内其他头文件
#include "common/global_thread_pool.h"
#include "rank_sub.pb.h"

DEFINE_int32(server_port, 8005, "服务器监听端口");
DEFINE_int32(sub_worker_count, 10, "子图数量");
DEFINE_string(sub_worker_addresses, "127.0.0.1:8006", "子图地址列表（逗号分隔）");
DEFINE_int32(top_k, 100, "返回前 K 个商品");
DEFINE_bool(enable_timing_stats, true, "是否启用详细时延统计");

namespace rank {

std::vector<uint64_t> parse_skus_from_string(const std::string& skus) {
    std::vector<uint64_t> sku_ids;
    
    if (skus.empty()) {
        LOG(WARNING) << "Empty skus string";
        return sku_ids;
    }
    
    // 每 6 位数字是一个商品 ID
    const size_t SKU_ID_LENGTH = 6;
    size_t pos = 0;
    
    while (pos + SKU_ID_LENGTH <= skus.size()) {
        std::string sku_str = skus.substr(pos, SKU_ID_LENGTH);
        
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

std::map<int, std::vector<uint64_t>> distribute_skus_by_hash(
    const std::vector<uint64_t>& sku_ids, 
    int n_workers) {
    
    std::map<int, std::vector<uint64_t>> distribution;
    
    // 初始化每个子图的列表
    for (int i = 0; i < n_workers; ++i) {
        distribution[i] = std::vector<uint64_t>();
    }
    
    // 使用哈希分配
    for (uint64_t sku_id : sku_ids) {
        size_t hash = std::hash<uint64_t>{}(sku_id);
        int worker_index = hash % n_workers;
        distribution[worker_index].push_back(sku_id);
    }
    
    // 打印分配统计
    for (int i = 0; i < n_workers; ++i) {
        LOG(INFO) << "Worker " << i << " assigned " << distribution[i].size() << " SKUs";
    }
    
    return distribution;
}

std::string skus_to_string(const std::vector<uint64_t>& sku_ids) {
    std::string result;
    for (uint64_t sku_id : sku_ids) {
        // 格式化为 6 位数字
        char buffer[8];
        snprintf(buffer, sizeof(buffer), "%06lu", sku_id);
        result += buffer;
    }
    return result;
}

RankMasterServiceImpl::RankMasterServiceImpl() 
    : sub_worker_channels_() {
    
    LOG(INFO) << "RankMasterServiceImpl initialized";
    LOG(INFO) << "Sub-worker count: " << FLAGS_sub_worker_count;
    LOG(INFO) << "Top-K: " << FLAGS_top_k;
    LOG(INFO) << "Sub-worker addresses: " << FLAGS_sub_worker_addresses;
    
    // 初始化子图 Channel 池
    std::vector<std::string> addresses;
    std::stringstream ss(FLAGS_sub_worker_addresses);
    std::string addr;
    
    while (std::getline(ss, addr, ',')) {
        // 去除空格
        addr.erase(0, addr.find_first_not_of(" "));
        addr.erase(addr.find_last_not_of(" ") + 1);
        if (!addr.empty()) {
            addresses.push_back(addr);
        }
    }
    
    // 如果地址数量少于子图数量，循环使用
    if (addresses.empty()) {
        LOG(ERROR) << "No sub-worker addresses provided, using default";
        addresses.push_back("127.0.0.1:8006");
    }
    
    // 创建 Channel
    for (int i = 0; i < FLAGS_sub_worker_count; ++i) {
        const std::string& worker_addr = addresses[i % addresses.size()];
        
        auto channel = std::make_unique<brpc::Channel>();
        brpc::ChannelOptions opts;
        opts.timeout_ms = 5000; // 5 秒超时
        opts.connection_type = "pooled"; // 使用连接池
        
        if (channel->Init(worker_addr.c_str(), &opts) == 0) {
            sub_worker_channels_.push_back(std::move(channel));
            LOG(INFO) << "Initialized channel to sub-worker " << i << ": " << worker_addr;
        } else {
            LOG(ERROR) << "Failed to initialize channel to sub-worker " << i 
                      << ": " << worker_addr;
            // 仍然添加一个空 channel，后续调用会失败
            sub_worker_channels_.push_back(std::move(channel));
        }
    }
}

RankMasterServiceImpl::~RankMasterServiceImpl() {
    LOG(INFO) << "RankMasterServiceImpl destroyed";
}

void RankMasterServiceImpl::Rank(google::protobuf::RpcController* controller,
                                 const RankMasterRequest* request,
                                 RankMasterResponse* response,
                                 google::protobuf::Closure* done) {
    
    brpc::ClosureGuard done_guard(done);
    (void)controller;
    
    auto& pool = common::get_global_thread_pool();
    
    // 提交任务到线程池
    auto future = pool.submit([this, request]() {
        // 创建响应对象
        RankMasterResponse local_response;
        
        // 处理请求
        process_rank_request(request, &local_response);
        
        return local_response;
    });
    
    // 等待任务完成
    try {
        RankMasterResponse result = future.get();
        response->CopyFrom(result);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Thread pool task failed: " << e.what();
    }
}

bool RankMasterServiceImpl::call_sub_worker(int worker_index,
                                           const std::string& user_feat_key,
                                           const std::vector<uint64_t>& sku_ids,
                                           RankSubResponse* response) {
    
    if (worker_index >= static_cast<int>(sub_worker_channels_.size()) || 
        !sub_worker_channels_[worker_index]) {
        LOG(ERROR) << "Invalid sub-worker index: " << worker_index;
        return false;
    }
    
    // 构造请求
    RankSubRequest request;
    request.set_user_feat_key(user_feat_key);
    request.set_skus_sub(skus_to_string(sku_ids));
    
    // 创建 Controller
    brpc::Controller cntl;
    
    // 创建 Stub
    rank::RankSubService_Stub stub(sub_worker_channels_[worker_index].get());
    
    // 发起调用
    stub.Rank(&cntl, &request, response, nullptr);
    
    if (cntl.Failed()) {
        LOG(ERROR) << "Sub-worker " << worker_index << " call failed: " 
                  << cntl.ErrorText();
        return false;
    }
    
    LOG(INFO) << "Sub-worker " << worker_index << " returned " 
              << response->skus_score_size() << " scores";
    
    return true;
}

void RankMasterServiceImpl::select_top_k(const std::map<uint64_t, double>& all_scores,
                                        int top_k,
                                        std::vector<uint64_t>& candidates) {
    
    // 转换为 vector 以便排序
    std::vector<std::pair<uint64_t, double>> score_vec(all_scores.begin(), all_scores.end());
    
    if (static_cast<int>(score_vec.size()) <= top_k) {
        // 全部排序
        std::sort(score_vec.begin(), score_vec.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        for (const auto& p : score_vec) {
            candidates.push_back(p.first);
        }
    } else {
        // 部分排序：前 K 个有序，后面无序
        std::nth_element(score_vec.begin(), 
                        score_vec.begin() + top_k, 
                        score_vec.end(),
                        [](const auto& a, const auto& b) { 
                            return a.second > b.second; 
                        });
        
        // 对前 K 个进行排序（保证有序）
        std::sort(score_vec.begin(), 
                 score_vec.begin() + top_k,
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // 提取前 K 个
        for (int i = 0; i < top_k; ++i) {
            candidates.push_back(score_vec[i].first);
        }
    }
    
    LOG(INFO) << "Selected top " << candidates.size() << " from " 
              << all_scores.size() << " candidates";
}

void RankMasterServiceImpl::process_rank_request(const RankMasterRequest* request,
                                                 RankMasterResponse* response) {
    
    int64_t server_receive_us = butil::gettimeofday_us();
    
    LOG(INFO) << "RankMaster request received";
    
    // 验证请求参数
    if (request->user_feat_key().empty()) {
        LOG(ERROR) << "Empty user_feat_key in request";
        return;
    }
    
    if (request->skus().empty()) {
        LOG(ERROR) << "Empty skus in request";
        return;
    }
    
    // 解析商品 ID 列表
    std::vector<uint64_t> all_sku_ids = parse_skus_from_string(request->skus());
    
    if (all_sku_ids.empty()) {
        LOG(ERROR) << "No SKU IDs parsed from skus";
        return;
    }
    
    LOG(INFO) << "Total SKU count: " << all_sku_ids.size();
    
    // 分配 SKU 到子图
    auto distribution = distribute_skus_by_hash(all_sku_ids, FLAGS_sub_worker_count);
    
    // 并行调用子图
    int64_t parallel_call_start_us = butil::gettimeofday_us();
    
    std::vector<std::future<std::pair<int, RankSubResponse>>> futures;
    
    for (int i = 0; i < FLAGS_sub_worker_count; ++i) {
        if (distribution[i].empty()) {
            LOG(INFO) << "Skipping empty worker " << i;
            continue;
        }
        
        futures.push_back(std::async(std::launch::async, [this, i, &request, &distribution]() {
            RankSubResponse sub_response;
            bool success = call_sub_worker(i, request->user_feat_key(), 
                                          distribution[i], &sub_response);
            return std::make_pair(success ? i : -1, sub_response);
        }));
    }
    
    // 收集所有子图的结果
    std::map<uint64_t, double> all_scores; // sku_id -> score
    
    for (auto& future : futures) {
        try {
            auto result = future.get();
            int worker_index = result.first;
            const RankSubResponse& sub_response = result.second;
            
            if (worker_index < 0) {
                LOG(WARNING) << "Worker " << worker_index << " failed";
                continue;
            }
            
            // 解析子图返回的 skus_id 和 skus_score（两个独立的数组）
            for (int i = 0; i < sub_response.skus_id_size(); ++i) {
                uint64_t sku_id = sub_response.skus_id(i);
                uint64_t score_int = sub_response.skus_score(i);
                double score = static_cast<double>(score_int) / 100.0;
                
                all_scores[sku_id] = score;
            }
        } catch (const std::exception& e) {
            LOG(ERROR) << "Exception caught while collecting sub-worker result: " 
                      << e.what();
        }
    }
    
    int64_t parallel_call_end_us = butil::gettimeofday_us();
    int64_t parallel_call_cost_us = parallel_call_end_us - parallel_call_start_us;
    
    LOG(INFO) << "Collected scores for " << all_scores.size() << " SKUs";
    
    // 选择 Top-K
    int64_t select_topk_start_us = butil::gettimeofday_us();
    
    std::vector<uint64_t> candidates;
    select_top_k(all_scores, FLAGS_top_k, candidates);
    
    int64_t select_topk_end_us = butil::gettimeofday_us();
    int64_t select_topk_cost_us = select_topk_end_us - select_topk_start_us;
    
    // 设置响应
    for (uint64_t candidate : candidates) {
        response->add_candidates(candidate);
    }
    
    int64_t server_send_us = butil::gettimeofday_us();
    int64_t server_process_us = server_send_us - server_receive_us;
    
    LOG(INFO) << "RankMaster processing completed:"
              << " input_skus=" << all_sku_ids.size()
              << " scored_skus=" << all_scores.size()
              << " output_candidates=" << response->candidates_size();
    
    if (FLAGS_enable_timing_stats) {
        LOG(INFO) << "Server timing breakdown:"
                  << " parallel_call_cost=" << parallel_call_cost_us / 1000.0 << " ms"
                  << " select_topk_cost=" << select_topk_cost_us / 1000.0 << " ms"
                  << " server_process_total=" << server_process_us / 1000.0 << " ms";
    }
    
    int64_t end_us = butil::gettimeofday_us();
    int64_t cost_us = end_us - server_receive_us;
    
    LOG(INFO) << "RankMaster completed, cost=" << cost_us / 1000.0 << " ms";
}

} // namespace rank
