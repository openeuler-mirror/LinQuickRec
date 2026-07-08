#ifndef COMMON_PERF_LOGGER_H
#define COMMON_PERF_LOGGER_H

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

#include "common/logger.h"
#include "common/perf_registry.h"

namespace common {
namespace perf {

inline void Log(const std::string& service,
                const std::string& stage,
                const std::string& metric,
                const std::string& trace_id,
                double duration_ms,
                const std::string& status = "ok",
                const std::string& extra = "") {
    std::ostringstream oss;
    oss << "PERF service=" << service
        << " stage=" << stage
        << " metric=" << metric
        << " trace_id=" << (trace_id.empty() ? "-" : trace_id)
        << " duration_ms=" << std::fixed << std::setprecision(3) << duration_ms
        << " status=" << status;
    if (!extra.empty()) {
        oss << " " << extra;
    }
    LOG_INFO << oss.str();

    // Dual-write to ring buffer for real-time collection
    if (PerfRingRegistry::Instance().Initialized()) {
        Span span;
        span.ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        span.SetService(service);
        span.SetStage(stage);
        span.SetMetric(metric);
        span.SetTraceId(trace_id);
        span.duration_ms = static_cast<float>(duration_ms);
        span.SetStatus(status);
        PerfRingRegistry::Instance().Push(span);
    }
}

inline double UsToMs(int64_t duration_us) {
    return duration_us / 1000.0;
}

} // namespace perf
} // namespace common

#endif // COMMON_PERF_LOGGER_H
