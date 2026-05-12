// Recall 服务单元测试
// 测试 proto_to_json、build_vllm_request、parse_vllm_response 纯逻辑函数

#include <cassert>
#include <iostream>
#include <string>

#include <rapidjson/document.h>

#include "recall_server.h"

using namespace recall;

// ============================================================================
// proto_to_json 测试
// ============================================================================

RecallRequest make_request(uint64_t user_id,
                           const std::vector<std::vector<uint32_t>>& logs = {},
                           const std::string& other = "") {
    RecallRequest req;
    req.set_user_id(user_id);
    for (const auto& vec : logs) {
        auto* log = req.add_user_logs();
        for (uint32_t v : vec) {
            log->add_vec(v);
        }
    }
    req.set_other(other);
    return req;
}

void test_proto_to_json_empty_request() {
    RecallRequest req = make_request(12345);
    std::string json = proto_to_json(&req);

    rapidjson::Document d;
    d.Parse(json.c_str());
    assert(!d.HasParseError());
    assert(d["user_id"].GetUint64() == 12345);
    assert(d.HasMember("user_logs"));
    assert(d["user_logs"].IsArray());
    assert(d["user_logs"].Size() == 0);

    std::cout << "[PASS] proto_to_json: empty request" << std::endl;
}

void test_proto_to_json_with_logs() {
    std::vector<std::vector<uint32_t>> logs = {{1, 2, 3}, {4, 5}};
    RecallRequest req = make_request(99, logs, "test_other");
    std::string json = proto_to_json(&req);

    rapidjson::Document d;
    d.Parse(json.c_str());
    assert(!d.HasParseError());
    assert(d["user_id"].GetUint64() == 99);
    assert(d["user_logs"].Size() == 2);
    assert(d["user_logs"][0]["vec"].Size() == 3);
    assert(d["user_logs"][0]["vec"][0].GetUint() == 1);
    assert(d["user_logs"][0]["vec"][2].GetUint() == 3);
    assert(d["user_logs"][1]["vec"].Size() == 2);
    assert(d.HasMember("other"));
    assert(std::string(d["other"].GetString()) == "test_other");

    std::cout << "[PASS] proto_to_json: with user_logs and other" << std::endl;
}

void test_proto_to_json_large_user_id() {
    RecallRequest req = make_request(18446744073709551615ULL); // uint64 max
    std::string json = proto_to_json(&req);

    rapidjson::Document d;
    d.Parse(json.c_str());
    assert(!d.HasParseError());
    assert(d["user_id"].GetUint64() == 18446744073709551615ULL);

    std::cout << "[PASS] proto_to_json: large user_id (uint64 max)" << std::endl;
}

// ============================================================================
// build_vllm_request 测试
// ============================================================================

void test_build_vllm_request_structure() {
    std::string input_json = R"({"user_id":42})";
    std::string vllm_json = build_vllm_request(input_json);

    rapidjson::Document d;
    d.Parse(vllm_json.c_str());
    assert(!d.HasParseError());

    assert(d.HasMember("model"));
    assert(d["model"].IsString());
    assert(std::string(d["model"].GetString()) == FLAGS_model_name);

    assert(d.HasMember("messages"));
    assert(d["messages"].IsArray());
    assert(d["messages"].Size() == 2);

    assert(d["messages"][0].HasMember("role"));
    assert(std::string(d["messages"][0]["role"].GetString()) == "system");
    assert(d["messages"][0].HasMember("content"));

    assert(d["messages"][1].HasMember("role"));
    assert(std::string(d["messages"][1]["role"].GetString()) == "user");
    assert(d["messages"][1].HasMember("content"));
    assert(std::string(d["messages"][1]["content"].GetString()).find(input_json) != std::string::npos);

    assert(d.HasMember("max_tokens"));
    assert(d["max_tokens"].GetInt() == 102400);

    assert(d.HasMember("temperature"));
    assert(d.HasMember("top_p"));
    assert(d.HasMember("stream"));
    assert(d["stream"].IsBool());
    assert(d["stream"].GetBool() == false);

    std::cout << "[PASS] build_vllm_request: structure validation" << std::endl;
}

void test_build_vllm_request_contains_sku_count() {
    std::string input_json = "{}";
    std::string vllm_json = build_vllm_request(input_json);

    std::string sku_count_str = std::to_string(FLAGS_sku_count);
    assert(vllm_json.find(sku_count_str) != std::string::npos);

    std::cout << "[PASS] build_vllm_request: contains sku_count=" << FLAGS_sku_count << std::endl;
}

// ============================================================================
// parse_vllm_response 测试
// ============================================================================

void test_parse_vllm_response_normal() {
    std::string body = R"({
        "choices": [{
            "message": {
                "content": "12345,67890,11111,22222,33333"
            }
        }]
    })";

    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 5);
    assert(ok);
    assert(response.sku_ids_size() == 5);
    assert(response.sku_ids(0) == 12345);
    assert(response.sku_ids(1) == 67890);
    assert(response.sku_ids(2) == 11111);
    assert(response.sku_ids(3) == 22222);
    assert(response.sku_ids(4) == 33333);

    std::cout << "[PASS] parse_vllm_response: normal response" << std::endl;
}

void test_parse_vllm_response_with_whitespace() {
    std::string body = R"({
        "choices": [{
            "message": {
                "content": " 12345 , 67890 , \"11111\" , 22222 "
            }
        }]
    })";

    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 4);
    assert(ok);
    assert(response.sku_ids_size() == 4);
    assert(response.sku_ids(0) == 12345);
    assert(response.sku_ids(1) == 67890);
    assert(response.sku_ids(2) == 11111);
    assert(response.sku_ids(3) == 22222);

    std::cout << "[PASS] parse_vllm_response: SKU with whitespace/quotes" << std::endl;
}

void test_parse_vllm_response_empty_string() {
    std::string body = "";
    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 10);
    assert(!ok);

    std::cout << "[PASS] parse_vllm_response: empty string returns false" << std::endl;
}

void test_parse_vllm_response_malformed_json() {
    std::string body = "{ this is not valid json }}}";
    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 10);
    assert(!ok);

    std::cout << "[PASS] parse_vllm_response: malformed JSON returns false" << std::endl;
}

void test_parse_vllm_response_no_choices() {
    std::string body = R"({"choices": []})";
    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 10);
    assert(!ok);

    std::cout << "[PASS] parse_vllm_response: empty choices returns false" << std::endl;
}

void test_parse_vllm_response_no_message() {
    std::string body = R"({"choices": [{}]})";
    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 10);
    assert(!ok);

    std::cout << "[PASS] parse_vllm_response: no message field returns false" << std::endl;
}

void test_parse_vllm_response_no_content() {
    std::string body = R"({"choices": [{"message": {}}]})";
    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 10);
    assert(!ok);

    std::cout << "[PASS] parse_vllm_response: no content field returns false" << std::endl;
}

void test_parse_vllm_response_non_numeric_tokens() {
    std::string body = R"({
        "choices": [{
            "message": {
                "content": "12345,abcde,67890,hello,11111"
            }
        }]
    })";

    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 5);
    assert(ok);
    assert(response.sku_ids_size() == 3);
    assert(response.sku_ids(0) == 12345);
    assert(response.sku_ids(1) == 67890);
    assert(response.sku_ids(2) == 11111);

    std::cout << "[PASS] parse_vllm_response: non-numeric tokens skipped" << std::endl;
}

void test_parse_vllm_response_only_non_numeric() {
    std::string body = R"({
        "choices": [{
            "message": {
                "content": "abc,def,xyz"
            }
        }]
    })";

    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 5);
    assert(!ok);
    assert(response.sku_ids_size() == 0);

    std::cout << "[PASS] parse_vllm_response: all non-numeric returns false" << std::endl;
}

void test_parse_vllm_response_single_sku() {
    std::string body = R"({
        "choices": [{
            "message": {
                "content": "99999"
            }
        }]
    })";

    RecallResponse response;
    bool ok = parse_vllm_response(body, &response, 1);
    assert(ok);
    assert(response.sku_ids_size() == 1);
    assert(response.sku_ids(0) == 99999);

    std::cout << "[PASS] parse_vllm_response: single SKU" << std::endl;
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    common::logger::InitializeDefault();

    std::cout << "=== Recall Service Unit Tests ===" << std::endl;

    // proto_to_json
    test_proto_to_json_empty_request();
    test_proto_to_json_with_logs();
    test_proto_to_json_large_user_id();

    // build_vllm_request
    test_build_vllm_request_structure();
    test_build_vllm_request_contains_sku_count();

    // parse_vllm_response
    test_parse_vllm_response_normal();
    test_parse_vllm_response_with_whitespace();
    test_parse_vllm_response_empty_string();
    test_parse_vllm_response_malformed_json();
    test_parse_vllm_response_no_choices();
    test_parse_vllm_response_no_message();
    test_parse_vllm_response_no_content();
    test_parse_vllm_response_non_numeric_tokens();
    test_parse_vllm_response_only_non_numeric();
    test_parse_vllm_response_single_sku();

    std::cout << "\n=== All Recall Tests Passed ===" << std::endl;
    return 0;
}
