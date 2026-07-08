#include "common/perf_registry.h"

namespace common {
namespace perf {

PerfRingBuffer::PerfRingBuffer(size_t capacity)
    : capacity_(capacity),
      buffer_(new Span[capacity]) {}

PerfRingBuffer::Snapshot PerfRingBuffer::TakeAll() {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);

    uint64_t w = write_pos_.load(std::memory_order_acquire);
    uint64_t r = read_pos_.load(std::memory_order_relaxed);

    Snapshot result;
    result.dropped = dropped_.exchange(0, std::memory_order_relaxed);

    for (uint64_t i = r; i < w; ++i) {
        result.spans.push_back(buffer_[i % capacity_]);
    }

    read_pos_.store(w, std::memory_order_release);

    return result;
}

PerfRingRegistry& PerfRingRegistry::Instance() {
    static PerfRingRegistry instance;
    return instance;
}

void PerfRingRegistry::Init(size_t capacity) {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (!initialized_.load(std::memory_order_relaxed)) {
        buffer_ = std::make_unique<PerfRingBuffer>(capacity);
        initialized_.store(true, std::memory_order_release);
    }
}

void PerfRingRegistry::Push(const Span& s) {
    if (initialized_.load(std::memory_order_acquire)) {
        buffer_->Push(s);
    }
}

PerfRingBuffer::Snapshot PerfRingRegistry::TakeAll() {
    if (!initialized_.load(std::memory_order_acquire)) {
        return {};
    }
    return buffer_->TakeAll();
}

} // namespace perf
} // namespace common
