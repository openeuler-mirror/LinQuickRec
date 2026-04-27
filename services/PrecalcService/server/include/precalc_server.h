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

#include "common/global_thread_pool.h"

using namespace datasystem;

DECLARE_int32(server_port);
DECLARE_string(kvworker_host);
DECLARE_int32(kvworker_port);
DECLARE_string(etcd_address);
DECLARE_double(precalc_result_size_mb);
DECLARE_int32(ttl_seconds);
DECLARE_int32(user_feat_key_size_kb);
DECLARE_bool(enable_timing_stats);
DECLARE_int32(payload_size_kb);

namespace precalc {

/**
 * @brief 生成当前时间的微秒级时间戳字符串
 * 
 * @return std::string 微秒级时间戳字符串
 */
std::string generate_timestamp();

/**
 * @brief 生成指定大小的前置计算结果（随机 tensor 数据）
 * 
 * @param size_mb 数据大小（MB）
 * @return std::string 生成的前置计算结果
 */
std::string generate_precalc_result(double size_mb);

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
     * @param controller RPC 控制器
     * @param request 请求对象
     * @param response 响应对象
     * @param done 完成回调
     */
    void Precalculate(google::protobuf::RpcController* controller,
                      const PrecalcRequest* request,
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
