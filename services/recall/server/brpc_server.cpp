/**
 * @file brpc_server.cpp
 * @brief 召回服务服务端
 * 
 * 优化内容：
 * 1. 使用 RapidJSON 处理 JSON 格式
 * 2. 使用线程池处理并发请求
 */

#include "recall.pb.h"
#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <sstream>

// ============================================================================
// 配置参数
// ============================================================================

DEFINE_string(vllm_base_url, "http://127.0.0.1:8000", "vLLM 服务基础 URL");
DEFINE_string(vllm_endpoint, "/v1/chat/completions", "vLLM 聊天接口端点");
DEFINE_string(model_name, "/workspace/share/Qwen3-0.6B/", "模型名称");
DEFINE_int32(server_port, 8001, "服务器监听端口");
DEFINE_int32(thread_pool_size, 128, "线程池大小（默认 128，根据 CPU 核心数动态调整）");
DEFINE_int32(vllm_timeout_ms, 5000, "vLLM 请求超时时间（毫秒）");

// ============================================================================
// 线程池实现
// ============================================================================

/**
 * @brief 线程池类
 * 
 * 提供固定数量的工作线程，用于异步处理耗时任务
 * 支持任务提交、异步执行、结果返回
 */
class ThreadPool {
public:
    /**
     * @brief 构造函数
     * 
     * @param num_threads 线程池中工作线程的数量
     */
    explicit ThreadPool(size_t num_threads) : stop_(false) {
        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                worker_loop();
            });
        }
    }

    /**
     * @brief 析构函数
     * 
     * 停止所有线程并等待任务完成
     */
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief 提交任务到线程池
     * 
     * @tparam F 函数类型
     * @tparam Args 参数类型
     * @param f 要执行的函数
     * @param args 函数参数
     * @return std::future<decltype(f(args...))> 返回 future 用于获取结果
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<decltype(f(args...))> {
        
        using return_type = decltype(f(args...));
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            if (stop_) {
                throw std::runtime_error("Cannot submit task to stopped ThreadPool");
            }
            
            tasks_.emplace([task]() {
                (*task)();
            });
        }
        
        condition_.notify_one();
        return result;
    }

    /**
     * @brief 获取线程池大小
     */
    size_t size() const {
        return workers_.size();
    }

    /**
     * @brief 获取待处理任务数量
     */
    size_t pending_tasks() const {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

private:
    /**
     * @brief 工作线程主循环
     */
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                
                condition_.wait(lock, [this] {
                    return stop_ || !tasks_.empty();
                });
                
                if (stop_ && tasks_.empty()) {
                    return;
                }
                
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            
            task();
        }
    }

    // 工作线程
    std::vector<std::thread> workers_;
    
    // 任务队列
    std::queue<std::function<void()>> tasks_;
    
    // 队列锁
    mutable std::mutex queue_mutex_;
    
    // 条件变量，用于通知任务到达
    std::condition_variable condition_;
    
    // 停止标志
    std::atomic<bool> stop_;
};

// ============================================================================
// vLLM 客户端工具函数
// ============================================================================

/**
 * @brief 将 Proto 请求转换为 JSON 格式
 * 
 * @param request Recall 请求
 * @return std::string JSON 字符串
 */
std::string proto_to_json(const recall::RecallRequest* request) {
    using namespace rapidjson;
    
    Document d;
    d.SetObject();
    Document::AllocatorType& allocator = d.GetAllocator();
    
    // 添加 user_id
    d.AddMember("user_id", static_cast<uint64_t>(request->user_id()), allocator);
    
    // 添加 user_logs
    Value user_logs(kArrayType);
    for (int i = 0; i < request->user_logs_size(); ++i) {
        const auto& log = request->user_logs(i);
        Value log_obj(kObjectType);
        
        Value vec(kArrayType);
        for (int j = 0; j < log.vec_size(); ++j) {
            vec.PushBack(log.vec(j), allocator);
        }
        
        log_obj.AddMember("vec", vec, allocator);
        user_logs.PushBack(log_obj, allocator);
    }
    d.AddMember("user_logs", user_logs, allocator);
    
    // 添加 other 字段
    d.AddMember("other", Value(request->other().c_str(), allocator).Move(), allocator);
    
    // 序列化为字符串
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    
    return buffer.GetString();
}

/**
 * @brief 构建符合 Qwen3-0.6B 的完整 API 请求
 * 
 * @param request_json Recall 请求的 JSON 表示
 * @return std::string 完整的 vLLM API 请求 JSON
 */
std::string build_vllm_request(const std::string& request_json) {
    using namespace rapidjson;
    
    Document d;
    d.SetObject();
    Document::AllocatorType& allocator = d.GetAllocator();
    
    // 添加 model 字段
    d.AddMember("model", Value(FLAGS_model_name, allocator).Move(), allocator);
    
    // 添加 messages 数组
    Value messages(kArrayType);
    
    // System message - few-shot prompt
    Value system_msg(kObjectType);
    system_msg.AddMember("role", "system", allocator);
    system_msg.AddMember("content", 
        "你是一个搜推广助手，请根据用户特征和日志返回推荐的 SKU ID 列表。", 
        allocator);
    messages.PushBack(system_msg, allocator);
    
    // User message - 包含请求数据
    Value user_msg(kObjectType);
    user_msg.AddMember("role", "user", allocator);
    
    // 构建 prompt 内容
    std::ostringstream prompt_ss;
    prompt_ss << "用户请求数据：" << request_json;
    user_msg.AddMember("content", Value(prompt_ss.str().c_str(), allocator).Move(), allocator);
    
    messages.PushBack(user_msg, allocator);
    d.AddMember("messages", messages, allocator);
    
    // 添加其他参数
    d.AddMember("max_tokens", 1024, allocator);
    d.AddMember("temperature", 0.7, allocator);
    d.AddMember("top_p", 0.9, allocator);
    d.AddMember("stream", false, allocator);
    
    // 序列化为字符串
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    
    return buffer.GetString();
}

/**
 * @brief 将大模型响应转换为 Proto 格式
 * 
 * @param response_body vLLM 的 JSON 响应
 * @param response Recall 响应对象
 * @return true 解析成功
 * @return false 解析失败
 */
bool parse_vllm_response(const std::string& response_body, 
                        recall::RecallResponse* response) {
    using namespace rapidjson;
    
    Document d;
    d.Parse(response_body.c_str());
    
    if (d.HasParseError()) {
        LOG(ERROR) << "JSON parse error at offset: " << d.GetErrorOffset();
        return false;
    }
    
    // 检查是否有 choices 数组
    if (!d.HasMember("choices") || !d["choices"].IsArray() || d["choices"].Size() == 0) {
        LOG(ERROR) << "No choices in response";
        return false;
    }
    
    const Value& first_choice = d["choices"][0];
    
    // 检查是否有 message
    if (!first_choice.HasMember("message") || 
        !first_choice["message"].HasMember("content")) {
        LOG(ERROR) << "No message content in response";
        return false;
    }
    
    const std::string content = first_choice["message"]["content"].GetString();
    
    // 尝试从 content 中提取 SKU IDs
    // 假设大模型返回的格式是：["SKU1", "SKU2", ...] 或 1,2,3,...
    // 这里需要根据实际情况调整解析逻辑
    
    // 简单示例：假设返回的是逗号分隔的数字
    std::istringstream iss(content);
    std::string token;
    while (std::getline(iss, token, ',')) {
        try {
            // 移除空格和引号
            token.erase(std::remove_if(token.begin(), token.end(), 
                                       [](char c) { return std::isspace(c) || c == '"'; }), 
                       token.end());
            
            if (!token.empty()) {
                uint64_t sku_id = std::stoull(token);
                response->add_sku_ids(sku_id);
            }
        } catch (const std::exception& e) {
            LOG(WARNING) << "Failed to parse SKU ID: " << token << ", error: " << e.what();
            // 继续解析下一个
        }
    }
    
    // 如果没有解析出任何 SKU ID，记录日志
    if (response->sku_ids_size() == 0) {
        LOG(WARNING) << "No SKU IDs parsed from response content: " << content;
        return false;
    }
    
    LOG(INFO) << "Successfully parsed " << response->sku_ids_size() 
              << " SKU IDs from response";
    
    return true;
}

// ============================================================================
// 服务实现类
// ============================================================================

/**
 * @brief 召回服务实现类
 */
class RecallServiceImpl : public recall::RecallService {
public:
    /**
     * @brief 构造函数
     */
    RecallServiceImpl() : thread_pool_(FLAGS_thread_pool_size) {
        LOG(INFO) << "RecallServiceImpl initialized with thread pool size: " 
                  << FLAGS_thread_pool_size;
    }

    /**
     * @brief 处理召回请求
     * 
     * @param cntl_base BRPC 控制器
     * @param request 请求对象
     * @param response 响应对象
     * @param done 完成回调
     */
    void Recall(google::protobuf::RpcController* cntl_base,
                const recall::RecallRequest* request,
                recall::RecallResponse* response,
                google::protobuf::Closure* done) override {
        
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        LOG(INFO) << "Recall request received, user_id: " << request->user_id();

        try {
            // 将耗时任务提交到线程池
            auto future = thread_pool_.submit([this, request]() {
                return process_recall_request(request);
            });

            // 等待任务完成
            auto result = future.get();

            // 填充响应
            if (result.success) {
                response->CopyFrom(result.response);
                LOG(INFO) << "Recall request processed successfully, user_id: " 
                         << request->user_id() 
                         << ", sku_count: " << response->sku_ids_size();
            } else {
                LOG(ERROR) << "Recall request failed: " << result.error_message;
                // 返回空响应或错误信息
            }

        } catch (const std::exception& e) {
            LOG(ERROR) << "Exception caught: " << e.what();
        }
    }

private:
    /**
     * @brief 召回请求处理结果
     */
    struct RecallResult {
        bool success = false;
        recall::RecallResponse response;
        std::string error_message;
    };

    /**
     * @brief 处理召回请求（在线程池中执行）
     * 
     * @param request 请求对象
     * @return RecallResult 处理结果
     */
    RecallResult process_recall_request(const recall::RecallRequest* request) {
        RecallResult result;

        // 1. 将 Proto 请求转换为 JSON
        std::string request_json = proto_to_json(request);
        LOG(INFO) << "Converted request to JSON: " << request_json;

        // 2. 构建 vLLM API 请求
        std::string vllm_json = build_vllm_request(request_json);
        LOG(INFO) << "Built vLLM request: " << vllm_json;

        // 3. 初始化 BRPC Channel
        brpc::Channel channel;
        brpc::ChannelOptions channel_opts;
        channel_opts.timeout_ms = FLAGS_vllm_timeout_ms;
        channel_opts.protocol = "http";

        std::string url = FLAGS_vllm_base_url + FLAGS_vllm_endpoint;
        if (channel.Init(url.c_str(), &channel_opts) != 0) {
            result.success = false;
            result.error_message = "Failed to initialize vLLM channel";
            return result;
        }

        // 4. 准备 HTTP 请求
        brpc::Controller http_cntl;
        http_cntl.http_request().uri() = FLAGS_vllm_endpoint;
        http_cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
        http_cntl.http_request().set_content_type("application/json");
        http_cntl.http_request().set_header("Host", "127.0.0.1:8000");
        http_cntl.http_request().set_header("User-Agent", "RecallService/1.0");
        http_cntl.http_request().set_header("Connection", "close");
        http_cntl.request_attachment().append(vllm_json);

        // 5. 同步调用 vLLM
        channel.CallMethod(nullptr, &http_cntl, nullptr, nullptr, nullptr);

        if (http_cntl.Failed()) {
            result.success = false;
            result.error_message = "vLLM service error: " + http_cntl.ErrorText();
            return result;
        }

        // 6. 解析 vLLM 响应
        const std::string& resp_body = http_cntl.response_attachment().to_string();
        LOG(INFO) << "vLLM response: " << resp_body;

        if (!parse_vllm_response(resp_body, &result.response)) {
            result.success = false;
            result.error_message = "Failed to parse vLLM response";
            return result;
        }

        result.success = true;
        return result;
    }

    // 线程池
    ThreadPool thread_pool_;
};

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char* argv[]) {
    // 1. 解析命令行参数
    google::ParseCommandLineFlags(&argc, &argv, true);

    // 2. 根据 CPU 核心数动态调整线程池大小
    int cpu_cores = std::thread::hardware_concurrency();
    if (cpu_cores > 0 && FLAGS_thread_pool_size == 128) {
        // 如果用户没有显式设置，使用 CPU 核心数的 2 倍
        FLAGS_thread_pool_size = std::max(4, cpu_cores * 2);
    }
    
    LOG(INFO) << "CPU cores: " << cpu_cores 
              << ", Thread pool size: " << FLAGS_thread_pool_size;

    // 3. 创建服务实例
    RecallServiceImpl service_impl;

    // 4. 创建 BRPC 服务器
    brpc::Server server;

    // 5. 注册服务
    if (server.AddService(static_cast<google::protobuf::Service*>(&service_impl),
                         brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add RecallService";
        return -1;
    }

    // 6. 启动服务器
    std::string server_addr = "0.0.0.0:" + std::to_string(FLAGS_server_port);
    if (server.Start(server_addr.c_str(), nullptr) != 0) {
        LOG(ERROR) << "Failed to start server on " << server_addr;
        return -1;
    }

    // 7. 打印启动信息
    LOG(INFO) << "===========================================";
    LOG(INFO) << "Recall Service Started";
    LOG(INFO) << "===========================================";
    LOG(INFO) << "Listening on: " << server_addr;
    LOG(INFO) << "vLLM Base URL: " << FLAGS_vllm_base_url;
    LOG(INFO) << "vLLM Endpoint: " << FLAGS_vllm_endpoint;
    LOG(INFO) << "Model Name: " << FLAGS_model_name;
    LOG(INFO) << "Thread Pool Size: " << FLAGS_thread_pool_size;
    LOG(INFO) << "vLLM Timeout: " << FLAGS_vllm_timeout_ms << "ms";
    LOG(INFO) << "===========================================";

    // 8. 等待退出信号
    server.RunUntilAskedToQuit();

    LOG(INFO) << "Recall Service stopped";
    return 0;
}
