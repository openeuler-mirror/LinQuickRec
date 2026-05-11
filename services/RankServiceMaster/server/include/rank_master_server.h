#ifndef RANK_MASTER_SERVER_H
#define RANK_MASTER_SERVER_H

#include "rank_master.pb.h"
#include "rank_sub.pb.h"
#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

#include "common/global_thread_pool.h"
#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"
#include "common/error.h"
#include "common/sku_utils.h"
#include "discovery_resolver.h"

DECLARE_int32(server_port);
DECLARE_int32(sub_worker_count);
DECLARE_string(sub_worker_addresses);
DECLARE_string(discovery_addr);
DECLARE_int32(top_k);
DECLARE_bool(enable_timing_stats);
DECLARE_int32(sub_worker_timeout_ms);

namespace rank {

using common::parse_skus_from_string;
using common::skus_to_string;
using common::distribute_skus_by_hash;

/**
 * @brief 精排主图服务实现类
 */
class RankMasterServiceImpl : public RankMasterService {
public:
    /**
     * @brief 构造函数
     */
    RankMasterServiceImpl();

    /**
     * @brief 析构函数
     */
    ~RankMasterServiceImpl();

    /**
     * @brief 处理精排请求
     * 
     * @param controller RPC 控制器
     * @param request 请求对象
     * @param response 响应对象
     * @param done 完成回调
     */
    void Rank(google::protobuf::RpcController* controller,
              const RankMasterRequest* request,
              RankMasterResponse* response,
              google::protobuf::Closure* done) override;

private:
    /**
     * @brief 实际处理精排请求的内部方法
     *
     * @param request 请求对象
     * @param response 响应对象
     * @return common::error::Status 处理状态
     */
    common::error::Status process_rank_request(const RankMasterRequest* request,
                                              RankMasterResponse* response);

    common::error::Status validate_and_parse(const RankMasterRequest* request,
                                              std::vector<uint64_t>& sku_ids);

    common::error::Status call_workers_and_aggregate(
        const RankMasterRequest* request,
        const std::vector<uint64_t>& all_sku_ids,
        std::map<uint64_t, double>& all_scores,
        const std::string& trace_id);

    /**
     * @brief 调用子图服务
     *
     * @param worker_index 子图索引
     * @param user_feat_key 用户特征 key
     * @param sku_ids 分配给该子图的 SKU ID 列表
     * @param trace_id 追踪 ID（传播到子图）
     * @param response 子图返回的响应
     * @return true 调用成功
     * @return false 调用失败
     */
    bool call_sub_worker(int worker_index,
                        const std::string& user_feat_key,
                        const std::vector<uint64_t>& sku_ids,
                        const std::string& trace_id,
                        RankSubResponse* response);

    /**
     * @brief 从所有子图结果中选择 Top-K 商品
     * 
     * @param all_scores 所有商品的打分结果 {sku_id: score}
     * @param top_k 返回前 K 个商品
     * @param candidates 输出的候选商品列表
     */
    void select_top_k(const std::map<uint64_t, double>& all_scores,
                     int top_k,
                     std::vector<uint64_t>& candidates);

    // 子图 Channel 池（复用连接）
    std::vector<std::unique_ptr<brpc::Channel>> sub_worker_channels_;

    // Discovery 查询器（可选，仅当 --discovery_addr 非空时使用）
    std::unique_ptr<DiscoveryResolver> discovery_resolver_;
};

} // namespace rank

#endif // RANK_MASTER_SERVER_H
