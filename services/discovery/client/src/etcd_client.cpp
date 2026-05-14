#include "etcd_client.h"

#include <cstring>
#include <sstream>

#include "common/logger.h"
#include "etcd_http.h"
#include "simple_json.h"

namespace discovery {

namespace {

const std::string BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static bool parseEndpoint(const std::string& ep, std::string& host, int& port) {
    size_t colon = ep.rfind(':');
    if (colon == std::string::npos) {
        host = ep;
        port = 2379;
        return true;
    }
    host = ep.substr(0, colon);
    try {
        port = std::stoi(ep.substr(colon + 1));
    } catch (...) {
        port = 2379;
    }
    return true;
}

} // namespace

EtcdClient::EtcdClient(const std::string& endpoints) {
    std::istringstream iss(endpoints);
    std::string ep;
    while (std::getline(iss, ep, ',')) {
        if (!ep.empty()) {
            endpoints_.push_back(ep);
        }
    }
    if (endpoints_.empty()) {
        endpoints_.push_back("127.0.0.1:2379");
    }
}

EtcdClient::~EtcdClient() = default;

std::string EtcdClient::post(const std::string& path,
                              const std::string& json_body) {
    if (current_endpoint_ >= endpoints_.size()) {
        current_endpoint_ = 0;
    }

    std::string host;
    int port = 2379;
    parseEndpoint(endpoints_[current_endpoint_], host, port);

    std::string response = etcd_http::post(host, port, path, json_body);
    if (response.empty()) {
        current_endpoint_++;
        if (current_endpoint_ < endpoints_.size()) {
            parseEndpoint(endpoints_[current_endpoint_], host, port);
            response = etcd_http::post(host, port, path, json_body);
        }
        current_endpoint_ = 0;
    }

    if (response.empty()) {
        LOG_ERROR << "etcd POST " << path << " failed";
    }
    return response;
}

std::string EtcdClient::base64Encode(const std::string& input) {
    std::string ret;
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            ret.push_back(BASE64_CHARS[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        ret.push_back(BASE64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (ret.size() % 4) {
        ret.push_back('=');
    }
    return ret;
}

std::string EtcdClient::base64Decode(const std::string& input) {
    std::string ret;
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        auto pos = BASE64_CHARS.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            ret.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return ret;
}

std::string EtcdClient::prefixEnd(const std::string& prefix) {
    std::string end = prefix;
    for (int i = static_cast<int>(end.size()) - 1; i >= 0; --i) {
        if (static_cast<unsigned char>(end[i]) < 0xFF) {
            end[i]++;
            end.resize(i + 1);
            return end;
        }
    }
    return "";
}

int64_t EtcdClient::leaseGrant(int64_t ttl_seconds) {
    simple_json::Value req_body = simple_json::Value::object();
    req_body["TTL"] = simple_json::Value(std::to_string(ttl_seconds));

    std::string resp = post("/v3/lease/grant", req_body.dump());
    if (resp.empty()) return 0;

    try {
        simple_json::Value resp_json = simple_json::Value::parse(resp);
        if (resp_json.contains("ID")) {
            return std::stoll(resp_json.get("ID").str());
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "leaseGrant parse error: " << e.what();
    }
    return 0;
}

bool EtcdClient::leaseRevoke(int64_t lease_id) {
    simple_json::Value req_body = simple_json::Value::object();
    req_body["ID"] = simple_json::Value(std::to_string(lease_id));

    std::string resp = post("/v3/kv/lease/revoke", req_body.dump());
    return !resp.empty();
}

bool EtcdClient::put(const std::string& key, const std::string& value,
                     int64_t lease_id) {
    simple_json::Value req_body = simple_json::Value::object();
    req_body["key"] = simple_json::Value(base64Encode(key));
    req_body["value"] = simple_json::Value(base64Encode(value));
    if (lease_id > 0) {
        req_body["lease"] = simple_json::Value(std::to_string(lease_id));
    }

    std::string resp = post("/v3/kv/put", req_body.dump());
    return !resp.empty();
}

bool EtcdClient::deleteKey(const std::string& key) {
    simple_json::Value req_body = simple_json::Value::object();
    req_body["key"] = simple_json::Value(base64Encode(key));

    std::string resp = post("/v3/kv/deleterange", req_body.dump());
    return !resp.empty();
}

std::vector<std::pair<std::string, std::string>> EtcdClient::range(
    const std::string& prefix) {

    simple_json::Value req_body = simple_json::Value::object();
    req_body["key"] = simple_json::Value(base64Encode(prefix));

    std::string end = prefixEnd(prefix);
    if (!end.empty()) {
        req_body["range_end"] = simple_json::Value(base64Encode(end));
    }

    std::string resp = post("/v3/kv/range", req_body.dump());
    if (resp.empty()) return {};

    std::vector<std::pair<std::string, std::string>> result;
    try {
        simple_json::Value resp_json = simple_json::Value::parse(resp);
        if (resp_json.contains("kvs")) {
            const auto& kvs = resp_json.get("kvs");
            for (size_t i = 0; i < kvs.size(); ++i) {
                const auto& kv = kvs[i];
                std::string k = base64Decode(kv.get("key").str());
                std::string v = base64Decode(kv.get("value").str());
                result.emplace_back(std::move(k), std::move(v));
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "range parse error: " << e.what();
    }
    return result;
}

} // namespace discovery
