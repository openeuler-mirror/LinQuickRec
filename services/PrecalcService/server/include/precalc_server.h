#ifndef PRECALC_SERVER_H
#define PRECALC_SERVER_H

#include "precalc.pb.h"
#include <brpc/server.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <datasystem/kv_client.h>

#include <string>
#include <memory>

using namespace datasystem;

DECLARE_int32(server_port);
DECLARE_string(kvworker_host);
DECLARE_int32(kvworker_port);
DECLARE_double(precalc_result_size_mb);
DECLARE_int32(ttl_seconds);
DECLARE_int32(response_total_size_kb);
DECLARE_bool(enable_timing_stats);

namespace precalc {

/**
 * @brief 生成当前时间的微秒级时间戳字符串
 * 
 * @return std::string 微秒级时间戳字符串
 */
std::string generate_timestamp();

/**
 * @brief 从用户特征数据中提取 user_id
 * 
 * @param user_feat 用户特征数据
 * @return uint64_t 用户 ID
 */
uint64_t extract_user_id(const std::string& user_feat);

/**
 * @brief 生成指定大小的前置计算结果（随机 tensor 数据）
 * 
 * @param size_mb 数据大小（MB）
 * @return std::string 生成的前置计算结果
 */
std::string generate_precalc_result(double size_mb);

/**
 * @brief 生成指定大小的随机 payload 数据（乱码），用于模拟负载
 * 
 * @param size_kb 数据大小（KB）
 * @return std::string 生成的 payload 数据
 */
std::string generate_payload(size_t size_kb);

/**
 * @brief 前置计算服务实现类
 */
class PrecalcServiceImpl : public PrecalcService {
public:
    /**
     * @brief 构造函数
     */
    PrecalcServiceImpl();

    /**
     * @brief 处理前置计算请求
     * 
     * @param request 请求对象
     * @param response 响应对象
     * @param done 完成回调
     */
    void Precalculate(const PrecalcRequest* request,
                      PrecalcResponse* response,
                      google::protobuf::Closure* done) override;

private:
    /**
     * @brief 实际处理前置计算请求的内部方法
     * 
     * @param request 请求对象
     * @param response 响应对象
     */
    void process_precalc_request(const PrecalcRequest* request,
                                  PrecalcResponse* response);
};

} // namespace precalc

#endif // PRECALC_SERVER_H
