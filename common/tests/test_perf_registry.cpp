#include "common/perf_registry.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace common::perf;

static void test_push_and_snapshot() {
    PerfRingBuffer buf(100);

    Span s;
    s.ts_us = 1000;
    s.SetService("proxy");
    s.SetStage("proxy_e2e");
    s.SetMetric("e2e");
    s.SetTraceId("test123");
    s.duration_ms = 10.5f;
    s.SetStatus("ok");

    for (int i = 0; i < 50; ++i) {
        buf.Push(s);
    }

    auto snap = buf.TakeAll();
    assert(snap.spans.size() == 50);
    assert(snap.dropped == 0);
    assert(std::string(snap.spans[0].service) == "proxy");

    // Verify buffer is now empty
    auto snap2 = buf.TakeAll();
    assert(snap2.spans.size() == 0);

    printf("[PASS] push_and_snapshot\n");
}

static void test_overflow() {
    PerfRingBuffer buf(100);

    Span s;
    s.SetService("test");
    s.SetStage("test");
    s.SetMetric("processing");
    s.SetTraceId("overflow");
    s.duration_ms = 1.0f;
    s.SetStatus("ok");

    for (int i = 0; i < 150; ++i) {
        buf.Push(s);
    }

    auto snap = buf.TakeAll();
    // Ring buffer wraps: only the last 100 items are available
    assert(snap.spans.size() == 100);
    assert(snap.dropped >= 50);

    printf("[PASS] overflow (dropped=%llu)\n", (unsigned long long)snap.dropped);
}

static void test_registry_singleton() {
    auto& r = PerfRingRegistry::Instance();
    assert(!r.Initialized());

    r.Init(200);

    assert(r.Initialized());

    Span s;
    s.SetService("test");
    s.SetStage("reg_test");
    s.SetMetric("processing");
    s.SetTraceId("reg");
    s.duration_ms = 5.0f;
    s.SetStatus("ok");

    for (int i = 0; i < 10; ++i) {
        r.Push(s);
    }

    auto snap = r.TakeAll();
    assert(snap.spans.size() == 10);

    // Init again is a no-op
    r.Init(500);
    assert(r.Initialized());

    printf("[PASS] registry_singleton\n");
}

static void test_push_before_init_is_noop() {
    // Create a fresh program-level test: Push before Init is safe
    Span s;
    s.SetService("nop");
    s.SetStage("nop");
    s.SetMetric("processing");
    s.SetTraceId("none");
    s.duration_ms = 0.0f;
    s.SetStatus("ok");

    // This should not crash even though registry wasn't explicitly init'd
    PerfRingRegistry::Instance().Push(s);

    // Second init still works
    PerfRingRegistry::Instance().Init(100);
    assert(PerfRingRegistry::Instance().Initialized());

    PerfRingRegistry::Instance().Push(s);
    auto snap = PerfRingRegistry::Instance().TakeAll();
    assert(snap.spans.size() == 1);

    printf("[PASS] push_before_init_is_noop\n");
}

int main() {
    printf("=== PerfRingBuffer Unit Tests ===\n");
    test_push_and_snapshot();
    test_overflow();
    test_registry_singleton();
    test_push_before_init_is_noop();
    printf("=== All tests passed ===\n");
    return 0;
}
