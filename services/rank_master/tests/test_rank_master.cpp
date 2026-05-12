// RankMaster 服务单元测试
// 测试 parse_skus_from_string、distribute_skus_by_hash、skus_to_string、select_top_k 纯逻辑函数

#include "rank_master_server.h"

#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace rank;

// ============================================================================
// parse_skus_from_string 测试
// ============================================================================

void test_parse_skus_empty() {
    auto result = parse_skus_from_string("");
    assert(result.empty());
    std::cout << "[PASS] parse_skus_from_string: empty string" << std::endl;
}

void test_parse_skus_normal() {
    // 6 位一组：000001, 000123, 999999
    std::string input = "000001000123999999";
    auto result = parse_skus_from_string(input);
    assert(result.size() == 3);
    assert(result[0] == 1);
    assert(result[1] == 123);
    assert(result[2] == 999999);
    std::cout << "[PASS] parse_skus_from_string: normal 3 SKUs" << std::endl;
}

void test_parse_skus_partial_trailing() {
    // 18 chars = 3 full SKUs, trailing "12" (2 chars, < 6) ignored
    std::string input = "00000100012399999912";
    auto result = parse_skus_from_string(input);
    assert(result.size() == 3);
    std::cout << "[PASS] parse_skus_from_string: trailing partial ignored" << std::endl;
}

void test_parse_skus_single() {
    std::string input = "123456";
    auto result = parse_skus_from_string(input);
    assert(result.size() == 1);
    assert(result[0] == 123456);
    std::cout << "[PASS] parse_skus_from_string: single SKU" << std::endl;
}

void test_parse_skus_short_string() {
    // 5 chars < 6, nothing parsed
    std::string input = "12345";
    auto result = parse_skus_from_string(input);
    assert(result.empty());
    std::cout << "[PASS] parse_skus_from_string: string shorter than 6 returns empty" << std::endl;
}

void test_parse_skus_many() {
    // 1000 SKUs
    std::string input;
    for (int i = 0; i < 1000; ++i) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%06d", i);
        input += buf;
    }
    auto result = parse_skus_from_string(input);
    assert(result.size() == 1000);
    assert(result[0] == 0);
    assert(result[999] == 999);
    std::cout << "[PASS] parse_skus_from_string: 1000 SKUs" << std::endl;
}

// ============================================================================
// skus_to_string 测试
// ============================================================================

void test_skus_to_string_empty() {
    std::string result = skus_to_string({});
    assert(result.empty());
    std::cout << "[PASS] skus_to_string: empty list" << std::endl;
}

void test_skus_to_string_single() {
    std::string result = skus_to_string({123456});
    assert(result == "123456");
    std::cout << "[PASS] skus_to_string: single SKU" << std::endl;
}

void test_skus_to_string_multiple() {
    std::string result = skus_to_string({1, 123, 999999});
    assert(result == "000001000123999999");
    std::cout << "[PASS] skus_to_string: multiple SKUs" << std::endl;
}

void test_skus_to_string_zero() {
    std::string result = skus_to_string({0});
    assert(result == "000000");
    std::cout << "[PASS] skus_to_string: zero SKU" << std::endl;
}

// ============================================================================
// roundtrip: skus_to_string ↔ parse_skus_from_string
// ============================================================================

void test_roundtrip() {
    std::vector<uint64_t> original = {1, 100, 999, 123456, 999999};
    std::string encoded = skus_to_string(original);
    auto decoded = parse_skus_from_string(encoded);
    assert(decoded == original);
    std::cout << "[PASS] roundtrip: skus_to_string -> parse_skus_from_string" << std::endl;
}

void test_roundtrip_large() {
    std::vector<uint64_t> original;
    for (uint64_t i = 0; i < 500; ++i) {
        original.push_back(i * 7 + 3);
    }
    std::string encoded = skus_to_string(original);
    auto decoded = parse_skus_from_string(encoded);
    assert(decoded == original);
    std::cout << "[PASS] roundtrip: 500 SKUs" << std::endl;
}

// ============================================================================
// distribute_skus_by_hash 测试
// ============================================================================

void test_distribute_total_preserved() {
    std::vector<uint64_t> skus = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int n_workers = 3;
    auto dist = distribute_skus_by_hash(skus, n_workers);

    size_t total = 0;
    for (const auto& [idx, vec] : dist) {
        total += vec.size();
    }
    assert(total == skus.size());
    std::cout << "[PASS] distribute_skus_by_hash: total SKU count preserved" << std::endl;
}

void test_distribute_all_workers_present() {
    std::vector<uint64_t> skus = {1, 2, 3, 4, 5, 6};
    int n_workers = 3;
    auto dist = distribute_skus_by_hash(skus, n_workers);
    assert(static_cast<int>(dist.size()) == n_workers);
    for (int i = 0; i < n_workers; ++i) {
        assert(dist.count(i) == 1);
    }
    std::cout << "[PASS] distribute_skus_by_hash: all workers present in result" << std::endl;
}

void test_distribute_deterministic() {
    std::vector<uint64_t> skus = {100, 200, 300, 400, 500};
    int n_workers = 4;
    auto dist1 = distribute_skus_by_hash(skus, n_workers);
    auto dist2 = distribute_skus_by_hash(skus, n_workers);
    for (int i = 0; i < n_workers; ++i) {
        assert(dist1[i] == dist2[i]);
    }
    std::cout << "[PASS] distribute_skus_by_hash: deterministic for same input" << std::endl;
}

void test_distribute_empty_skus() {
    std::vector<uint64_t> skus;
    int n_workers = 3;
    auto dist = distribute_skus_by_hash(skus, n_workers);
    for (const auto& [idx, vec] : dist) {
        assert(vec.empty());
    }
    std::cout << "[PASS] distribute_skus_by_hash: empty SKU list" << std::endl;
}

void test_distribute_single_worker() {
    std::vector<uint64_t> skus = {1, 2, 3};
    int n_workers = 1;
    auto dist = distribute_skus_by_hash(skus, n_workers);
    assert(dist[0].size() == 3);
    std::cout << "[PASS] distribute_skus_by_hash: single worker gets all" << std::endl;
}

// ============================================================================
// select_top_k 逻辑测试（复制核心逻辑独立测试）
// ============================================================================

void select_top_k_standalone(const std::map<uint64_t, double>& all_scores,
                             int top_k,
                             std::vector<uint64_t>& candidates) {
    std::vector<std::pair<uint64_t, double>> score_vec(all_scores.begin(), all_scores.end());
    if (static_cast<int>(score_vec.size()) <= top_k) {
        std::sort(score_vec.begin(), score_vec.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& p : score_vec) {
            candidates.push_back(p.first);
        }
    } else {
        std::nth_element(score_vec.begin(),
                        score_vec.begin() + top_k,
                        score_vec.end(),
                        [](const auto& a, const auto& b) {
                            return a.second > b.second;
                        });
        std::sort(score_vec.begin(),
                 score_vec.begin() + top_k,
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        for (int i = 0; i < top_k; ++i) {
            candidates.push_back(score_vec[i].first);
        }
    }
}

void test_select_top_k_basic() {
    std::map<uint64_t, double> scores = {{1, 10.0}, {2, 50.0}, {3, 30.0}, {4, 20.0}, {5, 40.0}};
    std::vector<uint64_t> candidates;
    select_top_k_standalone(scores, 3, candidates);
    assert(candidates.size() == 3);
    assert(candidates[0] == 2); // 50.0
    assert(candidates[1] == 5); // 40.0
    assert(candidates[2] == 3); // 30.0
    std::cout << "[PASS] select_top_k: top 3 from 5 items" << std::endl;
}

void test_select_top_k_all() {
    std::map<uint64_t, double> scores = {{1, 5.0}, {2, 3.0}, {3, 8.0}};
    std::vector<uint64_t> candidates;
    select_top_k_standalone(scores, 10, candidates);
    assert(candidates.size() == 3);
    assert(candidates[0] == 3); // 8.0
    assert(candidates[1] == 1); // 5.0
    assert(candidates[2] == 2); // 3.0
    std::cout << "[PASS] select_top_k: top_k > size returns all sorted" << std::endl;
}

void test_select_top_k_single() {
    std::map<uint64_t, double> scores = {{10, 1.0}, {20, 99.0}, {30, 50.0}};
    std::vector<uint64_t> candidates;
    select_top_k_standalone(scores, 1, candidates);
    assert(candidates.size() == 1);
    assert(candidates[0] == 20);
    std::cout << "[PASS] select_top_k: top 1" << std::endl;
}

void test_select_top_k_empty() {
    std::map<uint64_t, double> scores;
    std::vector<uint64_t> candidates;
    select_top_k_standalone(scores, 5, candidates);
    assert(candidates.empty());
    std::cout << "[PASS] select_top_k: empty scores" << std::endl;
}

void test_select_top_k_equal_scores() {
    std::map<uint64_t, double> scores = {{1, 50.0}, {2, 50.0}, {3, 50.0}};
    std::vector<uint64_t> candidates;
    select_top_k_standalone(scores, 2, candidates);
    assert(candidates.size() == 2);
    std::cout << "[PASS] select_top_k: equal scores" << std::endl;
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    common::logger::InitializeDefault();

    std::cout << "=== RankMaster Service Unit Tests ===" << std::endl;

    // parse_skus_from_string
    test_parse_skus_empty();
    test_parse_skus_normal();
    test_parse_skus_partial_trailing();
    test_parse_skus_single();
    test_parse_skus_short_string();
    test_parse_skus_many();

    // skus_to_string
    test_skus_to_string_empty();
    test_skus_to_string_single();
    test_skus_to_string_multiple();
    test_skus_to_string_zero();

    // roundtrip
    test_roundtrip();
    test_roundtrip_large();

    // distribute_skus_by_hash
    test_distribute_total_preserved();
    test_distribute_all_workers_present();
    test_distribute_deterministic();
    test_distribute_empty_skus();
    test_distribute_single_worker();

    // select_top_k
    test_select_top_k_basic();
    test_select_top_k_all();
    test_select_top_k_single();
    test_select_top_k_empty();
    test_select_top_k_equal_scores();

    std::cout << "\n=== All RankMaster Tests Passed ===" << std::endl;
    return 0;
}
