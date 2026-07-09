#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "common/logger.h"
#include "common/perf_registry.h"
#include "../src/stats_engine.h"
#include "../src/sqlite_store.h"
#include "../src/series_manager.h"
#include "../src/span_tree.h"

using namespace common::perf;
using namespace perf;

static void init_test_logger() {
    common::logger::LoggerConfig cfg;
    cfg.level = common::logger::LogLevel::ERROR;
    cfg.console_output = false;
    cfg.file_path = "";
    common::logger::Initialize(cfg);
}

// ---- StatsEngine tests ----

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

    // avg of 1..100 = 50.5
    double avg = st->Avg();
    assert(avg >= 50.0 && avg <= 51.0);

    // p50 ~ 50, p99 ~ 99
    assert(st->P50() >= 45 && st->P50() <= 55);
    assert(st->P99() >= 95 && st->P99() <= 100);

    printf("[PASS] stats_basic: count=%llu avg=%.2f p50=%.2f p99=%.2f\n",
           (unsigned long long)st->Count(), avg, st->P50(), st->P99());
}

// ---- SQLite Store tests ----

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

    // Query one trace
    auto results = store.QueryTrace("trace5");
    assert(results.size() == 1);
    assert(results[0].duration_ms == 50.0f);

    // Cleanup with 0 retention (should skip)
    store.Cleanup(0);

    printf("[PASS] sqlite_store: %d results for trace5\n", (int)results.size());
    std::remove(db_path);
}

// ---- Series Manager tests ----

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

// ---- Span Tree tests ----

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
    assert(tree.children.size() >= 3);  // proxy feature recall as service roots

    // Cross-service: proxy's feature_rpc should have feature's children
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

    test_stats_basic();
    test_sqlite_store();
    test_series_manager();
    test_span_tree();

    printf("=== All tests passed ===\n");
    return 0;
}
