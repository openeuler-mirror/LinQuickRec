#ifndef PERF_COLLECTOR_STATS_ENGINE_H
#define PERF_COLLECTOR_STATS_ENGINE_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace perf {

class WelfordRunningStats {
public:
    void Push(double value);
    void Sample(double value);

    uint64_t Count() const { return count_; }
    double Min() const { return min_; }
    double Max() const { return max_; }
    double Avg() const { return mean_; }
    double Variance() const;
    double StdDev() const { return std::sqrt(Variance()); }
    double P50() const;
    double P99() const;

private:
    double Percentile(double p) const;

    uint64_t count_ = 0;
    double mean_ = 0.0;
    double M2_ = 0.0;
    double min_ = std::numeric_limits<double>::max();
    double max_ = std::numeric_limits<double>::lowest();

    uint64_t count_sampled_ = 0;
    static constexpr size_t kReservoirMax = 1000;
    mutable std::vector<double> reservoir_;
    mutable std::mt19937_64 rng_{std::random_device{}()};
};

class StatsEngine {
public:
    static StatsEngine& Instance();

    void Push(const std::string& service, const std::string& stage,
              double duration_ms);
    const WelfordRunningStats* Get(const std::string& service,
                                    const std::string& stage) const;
    void ResetRecent();

private:
    struct StageStats {
        WelfordRunningStats all_time;
        WelfordRunningStats recent;
    };
    std::map<std::string, StageStats> stage_stats_;
};

} // namespace perf

#endif // PERF_COLLECTOR_STATS_ENGINE_H
