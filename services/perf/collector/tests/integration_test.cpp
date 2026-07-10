#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <brpc/channel.h>
#include <brpc/server.h>

#include "api_handlers.h"
#include "common/logger.h"
#include "common/perf_handler.h"
#include "common/perf_registry.h"
#include "series_manager.h"
#include "span_tree.h"
#include "sqlite_store.h"
#include "stats_engine.h"

using namespace common::perf;
using namespace perf;

static void init_test_logger() {
    common::logger::LoggerConfig cfg;
    cfg.level = common::logger::LogLevel::ERROR;
    cfg.console_output = false;
    cfg.file_path = "";
    common::logger::Initialize(cfg);
}

// ---- PerfRingRegistry tests ----

static void test_perf_registry_basic() {
    auto& reg = PerfRingRegistry::Instance();
    reg.Init(1000);

    Span s;
    s.ts_us = 1000001;
    s.SetService("proxy");
    s.SetStage("proxy_e2e");
    s.SetMetric("e2e");
    s.SetTraceId("trace-abc");
    s.duration_ms = 123.4f;
    s.SetStatus("ok");
    reg.Push(s);

    auto snap = reg.TakeAll();
    assert(snap.spans.size() == 1);
    assert(snap.spans[0].duration_ms == 123.4f);
    assert(std::string(snap.spans[0].service) == "proxy");
    assert(std::string(snap.spans[0].stage) == "proxy_e2e");
    assert(std::string(snap.spans[0].trace_id) == "trace-abc");

    auto snap2 = reg.TakeAll();
    assert(snap2.spans.empty());

    printf("[PASS] perf_registry_basic: push=%d take=%d\n",
           (int)snap.spans.size(), (int)snap2.spans.size());
}

static void test_perf_registry_overflow() {
    auto& reg = PerfRingRegistry::Instance();
    reg.Init(100);

    Span s;
    s.SetService("s");
    s.SetStage("st");
    s.SetMetric("m");
    s.SetTraceId("t");
    s.SetStatus("ok");

    for (int i = 0; i < 200; ++i) {
        s.duration_ms = static_cast<float>(i);
        reg.Push(s);
    }

    auto snap = reg.TakeAll();
    assert(snap.dropped >= 100);
    assert(snap.spans.size() <= 100);

    // Drain remaining
    reg.TakeAll();

    printf("[PASS] perf_registry_overflow: spans=%d dropped=%llu\n",
           (int)snap.spans.size(), (unsigned long long)snap.dropped);
}

// ---- SqliteStore edge cases ----

static void test_sqlite_empty_flush() {
    const char* db_path = "/tmp/test_perf_empty.db";
    std::remove(db_path);

    SqliteStore store;
    assert(store.Open(db_path));

    std::vector<Span> empty;
    store.Flush(empty);

    printf("[PASS] sqlite_empty_flush: no crash on empty batch\n");
    std::remove(db_path);
}

static void test_sqlite_bad_path() {
    SqliteStore store;
    assert(!store.Open("/nonexistent/dir/perf.db"));

    printf("[PASS] sqlite_bad_path: returns false for invalid path\n");
}

static void test_sqlite_cleanup() {
    const char* db_path = "/tmp/test_perf_cleanup.db";
    std::remove(db_path);

    SqliteStore store;
    assert(store.Open(db_path));

    // Insert old spans
    std::vector<Span> old_batch;
    for (int i = 0; i < 10; ++i) {
        Span s;
        s.ts_us = 1000 + i;  // very old timestamp
        s.SetService("p");
        s.SetStage("e");
        s.SetMetric("m");
        s.SetTraceId("old");
        s.duration_ms = 1.0f;
        s.SetStatus("ok");
        old_batch.push_back(s);
    }
    store.Flush(old_batch);
    assert(old_batch.empty());

    // Insert recent spans
    std::vector<Span> new_batch;
    for (int i = 0; i < 10; ++i) {
        Span s;
        s.ts_us = 999999999999999ULL;
        s.SetService("p");
        s.SetStage("e");
        s.SetMetric("m");
        s.SetTraceId("new");
        s.duration_ms = 2.0f;
        s.SetStatus("ok");
        new_batch.push_back(s);
    }
    store.Flush(new_batch);

    // Cleanup with 1 hour retention (should delete old, keep new)
    store.Cleanup(3600);

    auto old_results = store.QueryTrace("old");
    auto new_results = store.QueryTrace("new");
    assert(old_results.empty());
    assert(!new_results.empty());

    printf("[PASS] sqlite_cleanup: old=%d new=%d\n",
           (int)old_results.size(), (int)new_results.size());
    std::remove(db_path);
}

// ---- OutlierDetector tests ----

static void test_outlier_basic() {
    auto& od = OutlierDetector::Instance();
    assert(!od.IsOutlier(50.0, 50.0, 5.0, 3.0));  // z=0
    assert(!od.IsOutlier(60.0, 50.0, 5.0, 3.0));  // z=2
    assert(od.IsOutlier(70.0, 50.0, 5.0, 3.0));    // z=4 > 3

    printf("[PASS] outlier_basic: z-score thresholds correct\n");
}

static void test_outlier_edge() {
    auto& od = OutlierDetector::Instance();
    assert(!od.IsOutlier(100.0, 50.0, 0.0, 3.0));    // stddev=0 -> safe
    assert(od.IsOutlier(100.0, 50.0, 5.0, 0.0));      // threshold=0 -> always outlier
    assert(!od.IsOutlier(49.0, 50.0, 5.0, 100.0));    // huge threshold -> safe

    printf("[PASS] outlier_edge: stddev=0, threshold=0 handled\n");
}

// ---- BRPC Server + Service Registration ----
// These MUST pass: they exercise the exact code paths that were segfaulting.

static void test_brpc_services_registered() {
    const char* db_path = "/tmp/test_perf_brpc.db";
    std::remove(db_path);
    SqliteStore sqlite;
    assert(sqlite.Open(db_path));

    brpc::Server server;

    // This AddService was segfaulting before proto fix
    int ret = server.AddService(new PerfService, brpc::SERVER_OWNS_SERVICE);
    assert(ret == 0);

    // This AddService was segfaulting before proto fix
    ret = server.AddService(new ApiHandlerService(&sqlite), brpc::SERVER_OWNS_SERVICE);
    assert(ret == 0);

    brpc::ServerOptions opts;
    int port = 19876;
    ret = server.Start(port, &opts);
    assert(ret == 0);

    server.Stop(0);
    server.Join();
    std::remove(db_path);

    printf("[PASS] brpc_services_registered: PerfService+ApiHandlerService registered and started\n");
}

static void test_api_handler_health() {
    const char* db_path = "/tmp/test_perf_health.db";
    std::remove(db_path);
    SqliteStore sqlite;
    assert(sqlite.Open(db_path));

    brpc::Server server;
    server.AddService(new ApiHandlerService(&sqlite), brpc::SERVER_OWNS_SERVICE);

    brpc::ServerOptions opts;
    int port = 19877;
    assert(server.Start(port, &opts) == 0);

    brpc::Channel channel;
    brpc::ChannelOptions ch_opts;
    ch_opts.protocol = "http";
    ch_opts.timeout_ms = 2000;
    ch_opts.max_retry = 0;
    assert(channel.Init("127.0.0.1:" + std::to_string(port), &ch_opts) == 0);

    brpc::Controller cntl;
    cntl.http_request().uri() = "/api/v1/health";
    cntl.http_request().set_method(brpc::HTTP_METHOD_GET);
    channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    assert(!cntl.Failed());
    assert(cntl.http_response().status_code() == 200);

    std::string body = cntl.response_attachment().to_string();
    assert(body.find("ok") != std::string::npos);

    server.Stop(0);
    server.Join();
    std::remove(db_path);

    printf("[PASS] api_handler_health: 200 OK\n");
}

static void test_api_handler_stats() {
    auto& engine = StatsEngine::Instance();
    StatsEngine::Instance().ResetRecent();
    for (int i = 1; i <= 100; ++i) {
        engine.Push("proxy", "proxy_e2e", static_cast<double>(i));
    }

    const char* db_path = "/tmp/test_perf_stats.db";
    std::remove(db_path);
    SqliteStore sqlite;
    assert(sqlite.Open(db_path));

    brpc::Server server;
    server.AddService(new ApiHandlerService(&sqlite), brpc::SERVER_OWNS_SERVICE);

    brpc::ServerOptions opts;
    int port = 19878;
    assert(server.Start(port, &opts) == 0);

    brpc::Channel channel;
    brpc::ChannelOptions ch_opts;
    ch_opts.protocol = "http";
    ch_opts.timeout_ms = 2000;
    ch_opts.max_retry = 0;
    assert(channel.Init("127.0.0.1:" + std::to_string(port), &ch_opts) == 0);

    // Test missing params
    {
        brpc::Controller cntl;
        cntl.http_request().uri() = "/api/v1/stats/current";
        cntl.http_request().set_method(brpc::HTTP_METHOD_GET);
        channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
        assert(cntl.http_response().status_code() == 400);
    }

    // Test valid query
    {
        brpc::Controller cntl;
        cntl.http_request().uri() = "/api/v1/stats/current?service=proxy&stage=proxy_e2e";
        cntl.http_request().set_method(brpc::HTTP_METHOD_GET);
        channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
        assert(cntl.http_response().status_code() == 200);

        std::string body = cntl.response_attachment().to_string();
        assert(body.find("avg") != std::string::npos);
        assert(body.find("count") != std::string::npos);
    }

    // Test nonexistent service
    {
        brpc::Controller cntl;
        cntl.http_request().uri() = "/api/v1/stats/current?service=none&stage=none";
        cntl.http_request().set_method(brpc::HTTP_METHOD_GET);
        channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
        assert(cntl.http_response().status_code() == 404);
    }

    server.Stop(0);
    server.Join();
    std::remove(db_path);

    printf("[PASS] api_handler_stats: 200/400/404 responses correct\n");
}

static void test_api_handler_not_found() {
    const char* db_path = "/tmp/test_perf_nf.db";
    std::remove(db_path);
    SqliteStore sqlite;
    assert(sqlite.Open(db_path));

    brpc::Server server;
    server.AddService(new ApiHandlerService(&sqlite), brpc::SERVER_OWNS_SERVICE);

    brpc::ServerOptions opts;
    int port = 19879;
    assert(server.Start(port, &opts) == 0);

    brpc::Channel channel;
    brpc::ChannelOptions ch_opts;
    ch_opts.protocol = "http";
    ch_opts.timeout_ms = 2000;
    ch_opts.max_retry = 0;
    assert(channel.Init("127.0.0.1:" + std::to_string(port), &ch_opts) == 0);

    brpc::Controller cntl;
    cntl.http_request().uri() = "/api/v1/no_such_endpoint";
    cntl.http_request().set_method(brpc::HTTP_METHOD_GET);
    channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    assert(cntl.http_response().status_code() == 404);

    server.Stop(0);
    server.Join();
    std::remove(db_path);

    printf("[PASS] api_handler_not_found: 404 returned\n");
}

// ---- PerfService /debug/perf test ----

static void test_perf_service_dump() {
    auto& reg = PerfRingRegistry::Instance();
    reg.Init(100);

    Span s;
    s.ts_us = 2000000;
    s.SetService("proxy");
    s.SetStage("proxy_e2e");
    s.SetMetric("e2e");
    s.SetTraceId("dump-test");
    s.duration_ms = 88.8f;
    s.SetStatus("ok");
    reg.Push(s);

    brpc::Server server;
    server.AddService(new PerfService, brpc::SERVER_OWNS_SERVICE);

    brpc::ServerOptions opts;
    int port = 19880;
    assert(server.Start(port, &opts) == 0);

    brpc::Channel channel;
    brpc::ChannelOptions ch_opts;
    ch_opts.protocol = "http";
    ch_opts.timeout_ms = 2000;
    ch_opts.max_retry = 0;
    assert(channel.Init("127.0.0.1:" + std::to_string(port), &ch_opts) == 0);

    brpc::Controller cntl;
    cntl.http_request().uri() = "/debug/perf";
    cntl.http_request().set_method(brpc::HTTP_METHOD_GET);
    channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    assert(!cntl.Failed());
    assert(cntl.http_response().status_code() == 200);

    std::string body = cntl.response_attachment().to_string();
    assert(body.find("proxy_e2e") != std::string::npos);
    assert(body.find("spans") != std::string::npos);

    // Drain
    reg.TakeAll();

    server.Stop(0);
    server.Join();

    printf("[PASS] perf_service_dump: /debug/perf returns spans JSON\n");
}

// ---- Existing tests ----

static void test_stats_basic() {
    auto& engine = StatsEngine::Instance();

    for (int i = 1; i <= 100; ++i) {
        engine.Push("proxy", "proxy_e2e", static_cast<double>(i));
    }

    auto* st = engine.Get("proxy", "proxy_e2e");
    assert(st != nullptr);
    assert(st->Count() == 100);
    assert(st->Min() == 1.0);
    assert(st->Max() == 100.0);

    double avg = st->Avg();
    assert(avg >= 50.0 && avg <= 51.0);

    assert(st->P50() >= 45 && st->P50() <= 55);
    assert(st->P99() >= 95 && st->P99() <= 100);

    printf("[PASS] stats_basic: count=%llu avg=%.2f p50=%.2f p99=%.2f\n",
           (unsigned long long)st->Count(), avg, st->P50(), st->P99());
}

static void test_sqlite_store() {
    const char* db_path = "/tmp/test_perf_collector.db";
    std::remove(db_path);

    SqliteStore store;
    assert(store.Open(db_path));

    std::vector<Span> batch;
    for (int i = 0; i < 10; ++i) {
        Span s;
        s.ts_us = 1000000 + i * 1000;
        s.SetService("proxy");
        s.SetStage("proxy_e2e");
        s.SetMetric("e2e");
        s.SetTraceId("trace" + std::to_string(i));
        s.duration_ms = static_cast<float>(i * 10.0);
        s.SetStatus("ok");
        batch.push_back(s);
    }
    store.Flush(batch);
    assert(batch.empty());

    auto results = store.QueryTrace("trace5");
    assert(results.size() == 1);
    assert(results[0].duration_ms == 50.0f);

    store.Cleanup(0);

    printf("[PASS] sqlite_store: %d results for trace5\n", (int)results.size());
    std::remove(db_path);
}

static void test_series_manager() {
    auto& mgr = SeriesManager::Instance();

    int64_t id = mgr.Start("baseline", 1000);
    assert(id == 1);
    assert(mgr.ActiveSeriesId() == 1);

    mgr.IncrementSpanCount();
    mgr.IncrementSpanCount();

    mgr.Stop(2000);
    assert(mgr.ActiveSeriesId() == 0);

    auto list = mgr.List();
    assert(list.size() == 1);
    assert(list[0].name == "baseline");
    assert(list[0].span_count == 2);
    assert(!list[0].active);

    printf("[PASS] series_manager: id=%lld count=%llu\n",
           (long long)list[0].id, (unsigned long long)list[0].span_count);
}

static void test_span_tree() {
    std::vector<Span> spans;

    auto make_span = [](const char* svc, const char* stg, double ms) {
        Span s;
        s.SetService(svc);
        s.SetStage(stg);
        s.duration_ms = static_cast<float>(ms);
        s.SetStatus("ok");
        s.SetTraceId("tree_test");
        return s;
    };

    spans.push_back(make_span("proxy", "proxy_e2e", 100.0));
    spans.push_back(make_span("proxy", "feature_rpc", 30.0));
    spans.push_back(make_span("feature", "feature_total", 28.0));
    spans.push_back(make_span("feature", "generate_user_logs", 15.0));
    spans.push_back(make_span("proxy", "recall_rpc", 50.0));
    spans.push_back(make_span("recall", "recall_total", 48.0));
    spans.push_back(make_span("recall", "vllm_rpc", 40.0));
    spans.push_back(make_span("proxy", "recall_precalc_parallel_wait", 55.0));

    auto tree = BuildSpanTree(spans);
    assert(tree.stage == "trace");
    assert(tree.children.size() >= 3);

    bool found_cross = false;
    for (auto& c : tree.children) {
        if (c.stage == "proxy_e2e") {
            for (auto& cc : c.children) {
                if (cc.stage == "feature_rpc" && !cc.children.empty()) {
                    found_cross = true;
                }
            }
        }
    }
    assert(found_cross);

    printf("[PASS] span_tree: %d top-level children, cross-service linked\n",
           (int)tree.children.size());
}

int main() {
    init_test_logger();
    printf("=== Perf-collector Integration Tests ===\n");

    // P0: PerfRingRegistry
    test_perf_registry_basic();
    test_perf_registry_overflow();

    // P0: BRPC server + service registration (was segfaulting!)
    test_brpc_services_registered();
    test_perf_service_dump();

    // P0: API handlers via HTTP
    test_api_handler_health();
    test_api_handler_stats();
    test_api_handler_not_found();

    // P1: OutlierDetector
    test_outlier_basic();
    test_outlier_edge();

    // P1: SqliteStore edge cases
    test_sqlite_empty_flush();
    test_sqlite_bad_path();
    test_sqlite_cleanup();

    // Existing tests
    test_stats_basic();
    test_sqlite_store();
    test_series_manager();
    test_span_tree();

    printf("=== All tests passed (%d total) ===\n", 16);
    return 0;
}
