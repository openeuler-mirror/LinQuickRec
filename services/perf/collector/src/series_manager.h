#ifndef PERF_COLLECTOR_SERIES_MANAGER_H
#define PERF_COLLECTOR_SERIES_MANAGER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace perf {

struct Series {
    int64_t id = 0;
    std::string name;
    int64_t start_ts_us = 0;
    int64_t stop_ts_us = 0;
    bool active = false;
    uint64_t span_count = 0;
};

class SeriesManager {
public:
    static SeriesManager& Instance();

    // Start a new series. Returns the series id.
    int64_t Start(const std::string& name, int64_t ts_us);

    // Stop the active series.
    void Stop(int64_t ts_us);

    // Returns the active series id, or 0 if none active.
    uint64_t ActiveSeriesId() const {
        return active_id_.load(std::memory_order_acquire);
    }

    // Returns all series (active + stopped).
    std::vector<Series> List() const;

    // Increment span count for active series.
    void IncrementSpanCount();

private:
    SeriesManager() = default;

    static int64_t NextId();

    mutable std::mutex mutex_;
    std::vector<Series> series_;
    std::atomic<uint64_t> active_id_{0};
};

class OutlierDetector {
public:
    static OutlierDetector& Instance();

    // Returns true if value is an outlier for the given stats baseline.
    // baseline: optional running stats to compare against (if nullptr, returns false).
    bool IsOutlier(double value, double avg, double stddev, double threshold = 3.0) const;

private:
    OutlierDetector() = default;
};

} // namespace perf

#endif // PERF_COLLECTOR_SERIES_MANAGER_H
