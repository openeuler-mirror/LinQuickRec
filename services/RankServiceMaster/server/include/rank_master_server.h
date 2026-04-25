#ifndef RANK_MASTER_SERVER_H
#define RANK_MASTER_SERVER_H

#include "rank_master.pb.h"
#include "rank_sub.pb.h"
#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

DECLARE_int32(server_port);
DECLARE_int32(sub_worker_count);
DECLARE_string(sub_worker_addresses);
DECLARE_int32(top_k);
DECLARE_bool(enable_timing_stats);

namespace rank {

/**
 * @brief 从字符串中提取商品 ID 列表
 * 
 * @param skus 字符串格式的商品 ID，每 6 位数字是一个商品 ID
 * @return std::vector<uint64_t> 商品 ID 列表
 */
std::vector<uint64_t> parse_skus_from_string(const std::string& skus);

/**
 * @brief 使用哈希分配策略将 SKU 分配给子图
 * 
 * @param sku_ids 商品 ID 列表
 * @param n_workers 子图数量
 * @return std::map<int, std::vector<uint64_t>> 子图索引 -> SKU ID 列表
 */
std::map<int, std::vector<uint64_t>> distribute_skus_by_hash(
    const std::vector<uint64_t>& sku_ids, 
    int n_workers);

/**
 * @brief 将 SKU ID 列表转换为字符串（每 6 位一个商品 ID）
 * 
 * @param sku_ids SKU ID 列表
 * @return std::string 商品 ID 字符串
 */
std::string skus_to_string(const std::vector<uint64_t>& sku_ids);

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
     */
    void process_rank_request(const RankMasterRequest* request,
                              RankMasterResponse* response);

    /**
     * @brief 调用子图服务
     * 
     * @param worker_index 子图索引
     * @param user_feat_key 用户特征 key
     * @param sku_ids 分配给该子图的 SKU ID 列表
     * @param response 子图返回的响应
     * @return true 调用成功
     * @return false 调用失败
     */
    bool call_sub_worker(int worker_index,
                        const std::string& user_feat_key,
                        const std::vector<uint64_t>& sku_ids,
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
};

} // namespace rank

#endif // RANK_MASTER_SERVER_H
