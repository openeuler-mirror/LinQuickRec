#ifndef RECALL_SERVER_H
#define RECALL_SERVER_H

#include <atomic>
#include <memory>
#include <string>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <datasystem/kv_client.h>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "common/error.h"
#include "common/logger.h"
#include "recall.pb.h"
#include "vllm_client.h"

DECLARE_string(vllm_base_url);
DECLARE_string(vllm_endpoint);
DECLARE_string(model_name);
DECLARE_int32(server_port);
DECLARE_int32(vllm_timeout_ms);
DECLARE_int32(sku_count);
DECLARE_bool(enable_vllm);
DECLARE_string(kv_worker_service);
DECLARE_string(registry_backend);
DECLARE_string(discovery_addr);
DECLARE_string(etcd_endpoints);
DECLARE_int32(kvcache_ttl_seconds);
DECLARE_int32(kvcache_size_bytes);

DECLARE_string(vllm_connection_type);
DECLARE_int32(vllm_max_retry);
DECLARE_int32(vllm_connect_timeout_ms);
DECLARE_int32(vllm_backup_request_ms);

namespace recall {

/**
 * @brief 将 Proto 请求转换为 JSON 格式
 * 
 * @param request Recall 请求
 * @return std::string JSON 字符串
 */
std::string proto_to_json(const RecallRequest* request);

/**
 * @brief 构建符合 Qwen3-0.6B 的完整 API 请求
 * 
 * @param request_json Recall 请求的 JSON 表示
 * @return std::string 完整的 vLLM API 请求 JSON
 */
std::string build_vllm_request(const std::string& request_json);

/**
 * @brief 将大模型响应转换为 Proto 格式
 * 
 * @param response_body vLLM 的 JSON 响应
 * @param response Recall 响应对象
 * @param max_sku_count 最大 SKU 数量
 * @return true 解析成功
 * @return false 解析失败
 */
bool parse_vllm_response(const std::string& response_body, 
                        RecallResponse* response,
                        int max_sku_count);

/**
 * @brief 召回服务实现类
 */
class RecallServiceImpl : public RecallService {
public:
    /**
     * @brief 召回请求处理结果
     */
    struct RecallResult {
        bool success = false;
        RecallResponse response;
        std::string error_message;
        common::error::Status status;
    };

    /**
     * @brief 构造函数
     */
    RecallServiceImpl();

    /**
     * @brief 处理召回请求
     * 
     * @param controller RPC 控制器
     * @param request 请求对象
     * @param response 响应对象
     * @param done 完成回调
     */
    void Recall(google::protobuf::RpcController* controller,
                const RecallRequest* request,
                RecallResponse* response,
                google::protobuf::Closure* done) override;

private:
    /**
     * @brief 处理召回请求（在线程池中执行）
     */
    RecallResult process_recall_request(const RecallRequest* request);

    /**
     * @brief KVCache 路径：根据用户特征查找/生成 kvcache，随机生成 SKU
     */
    RecallResult process_kvcache_recall(const RecallRequest* request);

    /**
     * @brief 从用户特征派生 KVWorker key（"rc:" + 16-char hex hash）
     */
    std::string derive_kvcache_key(const RecallRequest* request);

    // vLLM HTTP 客户端
    VllmClient vllm_client_;

    // KVWorker 服务发现
    std::shared_ptr<datasystem::ServiceDiscovery> service_discovery_;
};

} // namespace recall

#endif // RECALL_SERVER_H
