#include <brpc/channel.h>
#include <brpc/controller.h>
#include <rapidjson/document.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "common/logger.h"
#include "common/perf_registry.h"
#include "common/service_discovery.h"
#include "series_manager.h"
#include "sqlite_store.h"
#include "stats_engine.h"

namespace perf {

namespace {

const std::vector<std::string> kTargetServices = {
    "proxy",
    "feature_service",
    "recall_service",
    "precalc_service",
    "rank_service",
    "rank_sub",
};

common::perf::Span SpanFromJson(const rapidjson::Value& v) {
    common::perf::Span s;
    s.ts_us = v["ts_us"].GetUint64();
    s.SetService(v["service"].GetString());
    s.SetStage(v["stage"].GetString());
    s.SetMetric(v["metric"].GetString());
    s.SetTraceId(v["trace_id"].GetString());
    s.duration_ms = static_cast<float>(v["duration_ms"].GetDouble());
    s.SetStatus(v["status"].GetString());
    return s;
}

bool PullService(const std::string& host, int port,
                 std::vector<common::perf::Span>& out_spans,
                 uint64_t& out_dropped) {
    brpc::Channel channel;
    brpc::ChannelOptions opts;
    opts.protocol = "http";
    opts.timeout_ms = 2000;
    opts.connection_type = "short";
    opts.max_retry = 0;

    std::string addr = host + ":" + std::to_string(port);
    if (channel.Init(addr.c_str(), &opts) != 0) {
        return false;
    }

    brpc::Controller cntl;
    cntl.http_request().uri() = "/debug/perf";
    cntl.http_request().set_method(brpc::HTTP_METHOD_GET);

    channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);

    if (cntl.Failed() || cntl.http_response().status_code() != 200) {
        return false;
    }

    butil::IOBuf& body = cntl.response_attachment();
    std::string json_str = body.to_string();

    rapidjson::Document doc;
    if (doc.Parse(json_str.c_str()).HasParseError()) {
        LOG_ERROR << "Failed to parse /debug/perf JSON from " << addr;
        return false;
    }

    out_dropped = doc["dropped"].GetUint64();
    for (auto& s : doc["spans"].GetArray()) {
        out_spans.push_back(SpanFromJson(s));
    }
    return true;
}

} // namespace

void StartPuller(
    std::shared_ptr<common::ServiceDiscovery> discovery,
    SqliteStore* sqlite,
    int retention_days) {

    LOG_INFO << "Puller started, pulling from " << kTargetServices.size()
             << " services every 1s";

    std::vector<common::perf::Span> batch;

    while (true) {
        auto start = std::chrono::steady_clock::now();

        for (const auto& service_name : kTargetServices) {
            std::string host;
            int port;
            std::string instance_id;
            if (!discovery->GetInstance(service_name, host, port, instance_id)) {
                continue;
            }

            std::vector<common::perf::Span> spans;
            uint64_t dropped = 0;
            if (PullService(host, port, spans, dropped)) {
                if (dropped > 0) {
                    LOG_WARN << "Ring buffer overflow on " << service_name
                             << ": " << dropped << " spans dropped";
                }

                for (auto& s : spans) {
                    StatsEngine::Instance().Push(s.service, s.stage, s.duration_ms);
                    batch.push_back(std::move(s));
                }

                if (SeriesManager::Instance().ActiveSeriesId() != 0) {
                    SeriesManager::Instance().IncrementSpanCount();
                }
            } else {
                LOG_ERROR << "Failed to pull /debug/perf from "
                          << service_name << " at " << host << ":" << port;
            }
        }

        if (!batch.empty()) {
            sqlite->Flush(batch);
        }
        sqlite->Cleanup(retention_days > 0 ? static_cast<int64_t>(retention_days) * 24 * 3600 : 0);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        auto sleep_ms = std::max<int64_t>(0, 1000 - elapsed);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}

} // namespace perf
