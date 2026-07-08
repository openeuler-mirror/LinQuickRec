#include "series_manager.h"

#include <algorithm>
#include <cmath>

namespace perf {

// ---- SeriesManager ----

SeriesManager& SeriesManager::Instance() {
    static SeriesManager instance;
    return instance;
}

int64_t SeriesManager::NextId() {
    static std::atomic<int64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

int64_t SeriesManager::Start(const std::string& name, int64_t ts_us) {
    std::lock_guard<std::mutex> lock(mutex_);

    Series s;
    s.id = NextId();
    s.name = name;
    s.start_ts_us = ts_us;
    s.active = true;
    series_.push_back(s);
    active_id_.store(static_cast<uint64_t>(s.id), std::memory_order_release);
    return s.id;
}

void SeriesManager::Stop(int64_t ts_us) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t id = active_id_.load(std::memory_order_acquire);
    for (auto& s : series_) {
        if (static_cast<uint64_t>(s.id) == id && s.active) {
            s.active = false;
            s.stop_ts_us = ts_us;
            break;
        }
    }
    active_id_.store(0, std::memory_order_release);
}

std::vector<Series> SeriesManager::List() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return series_;
}

void SeriesManager::IncrementSpanCount() {
    uint64_t id = active_id_.load(std::memory_order_acquire);
    if (id == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : series_) {
        if (static_cast<uint64_t>(s.id) == id && s.active) {
            s.span_count++;
            return;
        }
    }
}

// ---- OutlierDetector ----

OutlierDetector& OutlierDetector::Instance() {
    static OutlierDetector instance;
    return instance;
}

bool OutlierDetector::IsOutlier(double value, double avg, double stddev,
                                 double threshold) const {
    if (stddev <= 0.0) return false;
    double z = std::abs(value - avg) / stddev;
    return z > threshold;
}

} // namespace perf
