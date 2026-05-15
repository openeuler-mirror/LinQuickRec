#ifndef RANK_MASTER_SERVER_H
#define RANK_MASTER_SERVER_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "common/error.h"
#include "common/logger.h"
#include "common/sku_utils.h"
#include "rank_master.pb.h"
#include "rank_sub.pb.h"

DECLARE_int32(server_port);
DECLARE_string(discovery_addr);
DECLARE_int32(top_k);
DECLARE_int32(sub_worker_timeout_ms);
DECLARE_string(sub_worker_service_type);

DECLARE_string(sub_worker_connection_type);
DECLARE_int32(sub_worker_max_retry);
DECLARE_int32(sub_worker_connect_timeout_ms);
DECLARE_int32(sub_worker_backup_request_ms);
DECLARE_int32(sub_worker_parallelism);

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

    bool call_sub_worker(const std::string& user_feat_key,
                        const std::vector<uint64_t>& sku_ids,
                        const std::string& trace_id,
                        RankSubResponse* response);

    static void* sub_worker_bthread_fn(void* arg);

    void select_top_k(const std::map<uint64_t, double>& all_scores,
                     int top_k,
                     std::vector<uint64_t>& candidates);

    std::unique_ptr<brpc::Channel> sub_worker_channel_;
};

} // namespace rank

#endif // RANK_MASTER_SERVER_H
