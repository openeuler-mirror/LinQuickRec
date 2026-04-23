#ifndef RANK_SUB_SERVER_H
#define RANK_SUB_SERVER_H

#include "rank_sub.pb.h"
#include <brpc/server.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <datasystem/kv_client.h>

#include <string>
#include <vector>
#include <cstdint>

using namespace datasystem;

DECLARE_int32(server_port);
DECLARE_string(kvworker_host);
DECLARE_int32(kvworker_port);
DECLARE_string(etcd_address);
DECLARE_bool(enable_timing_stats);

namespace rank {

/**
 * @brief 从字符串中提取商品 ID 列表
 * 
 * @param skus_sub 字符串格式的商品 ID，每 6 位数字是一个商品 ID
 * @return std::vector<uint64_t> 商品 ID 列表
 */
std::vector<uint64_t> parse_skus_from_string(const std::string& skus_sub);

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
     */
    void process_rank_request(const RankSubRequest* request,
                              RankSubResponse* response);
};

} // namespace rank

#endif // RANK_SUB_SERVER_H
