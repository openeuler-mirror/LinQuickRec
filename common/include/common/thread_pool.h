#ifndef COMMON_THREAD_POOL_H
#define COMMON_THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <memory>

namespace common {

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
     * @param num_threads 线程池中工作线程的数量
     */
    explicit ThreadPool(size_t num_threads);

    /**
     * @brief 析构函数
     * 
     * 停止所有线程并等待任务完成
     */
    ~ThreadPool();

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
    size_t size() const;

    /**
     * @brief 获取待处理任务数量
     */
    size_t pending_tasks() const;

private:
    /**
     * @brief 工作线程主循环
     */
    void worker_loop();

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

} // namespace common

#endif // COMMON_THREAD_POOL_H
