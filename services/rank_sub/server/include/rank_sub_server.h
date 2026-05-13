#ifndef RANK_SUB_SERVER_H
#define RANK_SUB_SERVER_H

#include <cstdint>
#include <string>
#include <vector>

#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "common/error.h"
#include "common/global_thread_pool.h"
#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"
#include "common/sku_utils.h"
#include "rank_sub.pb.h"

DECLARE_int32(server_port);
DECLARE_string(kvworker_host);
DECLARE_int32(kvworker_port);
DECLARE_string(etcd_address);
DECLARE_bool(enable_timing_stats);

namespace rank {

using common::parse_skus_from_string;

/**
 * @brief 模拟打分逻辑
 * 
 * @param sku_id 商品 ID
 * @param user_feat 用户特征数据
 * @return double 打分结果
 */
double simulate_score(uint64_t sku_id, const std::string& user_feat);

/**
 * @brief 精排子图服务实现类
 */
class RankSubServiceImpl : public RankSubService {
public:
    /**
     * @brief 构造函数
     */
    RankSubServiceImpl();

    /**
     * @brief 处理精排请求
     * 
     * @param controller RPC 控制器
     * @param request 请求对象
     * @param response 响应对象
     * @param done 完成回调
     */
    void Rank(google::protobuf::RpcController* controller,
              const RankSubRequest* request,
              RankSubResponse* response,
              google::protobuf::Closure* done) override;

private:
    /**
     * @brief 实际处理精排请求的内部方法
     *
     * @param request 请求对象
     * @param response 响应对象
     * @return common::error::Status 处理状态
     */
    common::error::Status process_rank_request(const RankSubRequest* request,
                                              RankSubResponse* response);
};

} // namespace rank

#endif // RANK_SUB_SERVER_H
