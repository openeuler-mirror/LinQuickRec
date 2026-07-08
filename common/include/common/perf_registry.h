#ifndef COMMON_PERF_REGISTRY_H
#define COMMON_PERF_REGISTRY_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace common {
namespace perf {

struct Span {
    uint64_t ts_us = 0;
    char service[16] = {};
    char stage[32] = {};
    char metric[16] = {};
    char trace_id[33] = {};
    float duration_ms = 0.0f;
    char status[8] = {};

    void SetService(const std::string& s) {
        std::strncpy(service, s.c_str(), sizeof(service) - 1);
    }
    void SetStage(const std::string& s) {
        std::strncpy(stage, s.c_str(), sizeof(stage) - 1);
    }
    void SetMetric(const std::string& s) {
        std::strncpy(metric, s.c_str(), sizeof(metric) - 1);
    }
    void SetTraceId(const std::string& s) {
        std::strncpy(trace_id, s.c_str(), sizeof(trace_id) - 1);
    }
    void SetStatus(const std::string& s) {
        std::strncpy(status, s.c_str(), sizeof(status) - 1);
    }
};

class PerfRingBuffer {
public:
    explicit PerfRingBuffer(size_t capacity = 50000);

    PerfRingBuffer(const PerfRingBuffer&) = delete;
    PerfRingBuffer& operator=(const PerfRingBuffer&) = delete;

    inline void Push(const Span& s) {
        uint64_t pos = write_pos_.fetch_add(1, std::memory_order_relaxed);
        uint64_t r = read_pos_.load(std::memory_order_acquire);
        if (pos - r >= capacity_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        buffer_[pos % capacity_] = s;
    }

    struct Snapshot {
        std::vector<Span> spans;
        uint64_t dropped = 0;
    };

    Snapshot TakeAll();

    size_t Capacity() const { return capacity_; }
    uint64_t DroppedTotal() const { return dropped_.load(std::memory_order_relaxed); }

private:
    size_t capacity_;
    std::unique_ptr<Span[]> buffer_;
    std::atomic<uint64_t> write_pos_{0};
    std::atomic<uint64_t> read_pos_{0};
    std::atomic<uint64_t> dropped_{0};
    mutable std::mutex snapshot_mutex_;
};

class PerfRingRegistry {
public:
    static PerfRingRegistry& Instance();

    void Init(size_t capacity = 50000);
    void Push(const Span& s);
    PerfRingBuffer::Snapshot TakeAll();

    bool Initialized() const { return initialized_.load(std::memory_order_acquire); }

private:
    PerfRingRegistry() = default;
    std::mutex init_mutex_;
    std::atomic<bool> initialized_{false};
    std::unique_ptr<PerfRingBuffer> buffer_;
};

} // namespace perf
} // namespace common

#endif // COMMON_PERF_REGISTRY_H
