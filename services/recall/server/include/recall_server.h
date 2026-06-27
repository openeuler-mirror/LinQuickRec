#ifndef RECALL_SERVER_H
#define RECALL_SERVER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

class VllmClient;

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

    bool IsReady() const { return ready_; }

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
     * @brief novllm KVCache path with simulated hit/miss latency.
     */
    RecallResult process_kvcache_recall(const RecallRequest* request);

    /**
     * @brief Ensure the fixed novllm KVCache key is written once globally.
     */
    common::error::Status ensure_global_kvcache(datasystem::KVClient& kv_client,
                                                const std::string& trace_id);
    common::error::Status write_global_kvcache(datasystem::KVClient& kv_client,
                                               const std::vector<uint8_t>& value,
                                               const std::string& trace_id,
                                               const std::string& op_tag);

    // vLLM HTTP 客户端
    std::unique_ptr<VllmClient> vllm_client_;

    // KVWorker 服务发现
    std::shared_ptr<datasystem::ServiceDiscovery> service_discovery_;

    std::mutex global_kvcache_mutex_;
    bool global_kvcache_initialized_ = false;
    std::vector<uint8_t> global_kvcache_value_;
    bool ready_ = true;
};

} // namespace recall

#endif // RECALL_SERVER_H
