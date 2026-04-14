#ifndef LATENCY_METRICS_H
#define LATENCY_METRICS_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <algorithm>

/**
 * @brief 延迟统计指标
 * 
 * 用于收集和统计延迟数据，支持计算平均值、P99 等指标
 */
class LatencyMetrics {
public:
    /**
     * @brief 构造函数
     */
    LatencyMetrics() : count_(0), total_us_(0), min_us_(INT64_MAX), max_us_(0) {}

    /**
     * @brief 记录一次延迟
     * 
     * @param latency_us 延迟（微秒）
     */
    void record(int64_t latency_us) {
        if (latency_us < 0) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.push_back(latency_us);
            
            // 保持 samples_ 大小在合理范围内
            if (samples_.size() > MAX_SAMPLES) {
                samples_.erase(samples_.begin(), 
                             samples_.begin() + (samples_.size() / 2));
            }
        }

        count_.fetch_add(1, std::memory_order_relaxed);
        total_us_.fetch_add(latency_us, std::memory_order_relaxed);

        // 更新最小值（使用原子操作的 CAS）
        int64_t current_min = min_us_.load(std::memory_order_relaxed);
        while (latency_us < current_min) {
            if (min_us_.compare_exchange_weak(current_min, latency_us,
                                            std::memory_order_relaxed)) {
                break;
            }
        }

        // 更新最大值（使用原子操作的 CAS）
        int64_t current_max = max_us_.load(std::memory_order_relaxed);
        while (latency_us > current_max) {
            if (max_us_.compare_exchange_weak(current_max, latency_us,
                                            std::memory_order_relaxed)) {
                break;
            }
        }
    }

    /**
     * @brief 获取平均延迟（毫秒）
     * 
     * @return double 平均延迟
     */
    double get_average_ms() const {
        int64_t count = count_.load(std::memory_order_relaxed);
        if (count == 0) {
            return 0.0;
        }
        return static_cast<double>(total_us_.load(std::memory_order_relaxed)) 
               / count / 1000.0;
    }

    /**
     * @brief 获取 P99 延迟（毫秒）
     * 
     * @return double P99 延迟
     */
    double get_p99_ms() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) {
            return 0.0;
        }

        std::vector<int64_t> sorted_samples = samples_;
        std::sort(sorted_samples.begin(), sorted_samples.end());

        size_t p99_index = (sorted_samples.size() * 99) / 100;
        if (p99_index >= sorted_samples.size()) {
            p99_index = sorted_samples.size() - 1;
        }

        return static_cast<double>(sorted_samples[p99_index]) / 1000.0;
    }

    /**
     * @brief 获取 P95 延迟（毫秒）
     * 
     * @return double P95 延迟
     */
    double get_p95_ms() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) {
            return 0.0;
        }

        std::vector<int64_t> sorted_samples = samples_;
        std::sort(sorted_samples.begin(), sorted_samples.end());

        size_t p95_index = (sorted_samples.size() * 95) / 100;
        if (p95_index >= sorted_samples.size()) {
            p95_index = sorted_samples.size() - 1;
        }

        return static_cast<double>(sorted_samples[p95_index]) / 1000.0;
    }

    /**
     * @brief 获取最小延迟（毫秒）
     * 
     * @return double 最小延迟
     */
    double get_min_ms() const {
        int64_t min_us = min_us_.load(std::memory_order_relaxed);
        if (min_us == INT64_MAX) {
            return 0.0;
        }
        return static_cast<double>(min_us) / 1000.0;
    }

    /**
     * @brief 获取最大延迟（毫秒）
     * 
     * @return double 最大延迟
     */
    double get_max_ms() const {
        int64_t max_us = max_us_.load(std::memory_order_relaxed);
        return static_cast<double>(max_us) / 1000.0;
    }

    /**
     * @brief 获取请求总数
     * 
     * @return int64_t 请求总数
     */
    int64_t get_count() const {
        return count_.load(std::memory_order_relaxed);
    }

    /**
     * @brief 重置统计
     */
    void reset() {
        count_.store(0, std::memory_order_relaxed);
        total_us_.store(0, std::memory_order_relaxed);
        min_us_.store(INT64_MAX, std::memory_order_relaxed);
        max_us_.store(0, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.clear();
        }
    }

private:
    static constexpr size_t MAX_SAMPLES = 10000;

    // 请求计数
    std::atomic<int64_t> count_;
    
    // 总延迟（微秒）
    std::atomic<int64_t> total_us_;
    
    // 最小延迟（微秒）
    std::atomic<int64_t> min_us_;
    
    // 最大延迟（微秒）
    std::atomic<int64_t> max_us_;

    // 延迟样本（用于计算百分位数）
    mutable std::vector<int64_t> samples_;
    mutable std::mutex mutex_;
};

/**
 * @brief 延迟自动计算器
 * 
 * RAII 风格的延迟计算器，自动记录开始和结束时间
 */
class LatencyAutoCalculator {
public:
    /**
     * @brief 构造函数
     * 
     * @param metrics 延迟统计对象
     */
    explicit LatencyAutoCalculator(LatencyMetrics& metrics)
        : metrics_(metrics), start_us_(get_current_time_us()) {}

    /**
     * @brief 析构函数
     * 
     * 自动记录延迟
     */
    ~LatencyAutoCalculator() {
        int64_t end_us = get_current_time_us();
        metrics_.record(end_us - start_us_);
    }

    /**
     * @brief 手动记录延迟并提前结束
     * 
     * @param latency_us 延迟（微秒）
     */
    void record_now(int64_t latency_us) {
        if (!recorded_) {
            metrics_.record(latency_us);
            recorded_ = true;
        }
    }

private:
    /**
     * @brief 获取当前时间（微秒）
     * 
     * @return int64_t 当前时间戳
     */
    static int64_t get_current_time_us() {
        auto now = std::chrono::steady_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    }

    LatencyMetrics& metrics_;
    int64_t start_us_;
    bool recorded_ = false;
};

#endif // LATENCY_METRICS_H
