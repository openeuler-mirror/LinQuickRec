#include "etcd_client.h"

#include <cstring>
#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "common/logger.h"
#include "etcd_http.h"

using namespace rapidjson;

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

static std::string stringify(const Document& d) {
    StringBuffer buf;
    Writer<StringBuffer> w(buf);
    d.Accept(w);
    return buf.GetString();
}

static bool hasError(const std::string& json_resp) {
    if (json_resp.empty()) return true;
    Document d;
    d.Parse(json_resp.c_str());
    if (d.HasParseError()) return true;
    if (d.HasMember("error")) {
        LOG_ERROR << "etcd error: " << d["error"].GetString();
        return true;
    }
    return false;
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
    Document d;
    d.SetObject();
    auto& alloc = d.GetAllocator();
    d.AddMember("TTL", Value(std::to_string(ttl_seconds).c_str(), alloc), alloc);

    std::string resp = post("/v3/lease/grant", stringify(d));
    if (resp.empty() || hasError(resp)) return 0;

    Document r;
    r.Parse(resp.c_str());
    if (r.HasParseError()) return 0;
    if (r.HasMember("ID") && r["ID"].IsString()) {
        return std::stoll(r["ID"].GetString());
    }
    return 0;
}

bool EtcdClient::leaseRevoke(int64_t lease_id) {
    Document d;
    d.SetObject();
    auto& alloc = d.GetAllocator();
    d.AddMember("ID", Value(std::to_string(lease_id).c_str(), alloc), alloc);

    std::string resp = post("/v3/kv/lease/revoke", stringify(d));
    return !resp.empty() && !hasError(resp);
}

bool EtcdClient::put(const std::string& key, const std::string& value,
                     int64_t lease_id) {
    Document d;
    d.SetObject();
    auto& alloc = d.GetAllocator();
    d.AddMember("key", Value(base64Encode(key).c_str(), alloc), alloc);
    d.AddMember("value", Value(base64Encode(value).c_str(), alloc), alloc);
    if (lease_id > 0) {
        d.AddMember("lease", Value(std::to_string(lease_id).c_str(), alloc), alloc);
    }

    std::string resp = post("/v3/kv/put", stringify(d));
    return !resp.empty() && !hasError(resp);
}

bool EtcdClient::deleteKey(const std::string& key) {
    Document d;
    d.SetObject();
    auto& alloc = d.GetAllocator();
    d.AddMember("key", Value(base64Encode(key).c_str(), alloc), alloc);

    std::string resp = post("/v3/kv/deleterange", stringify(d));
    return !resp.empty() && !hasError(resp);
}

std::vector<std::pair<std::string, std::string>> EtcdClient::range(
    const std::string& prefix) {

    Document d;
    d.SetObject();
    auto& alloc = d.GetAllocator();
    d.AddMember("key", Value(base64Encode(prefix).c_str(), alloc), alloc);

    std::string end = prefixEnd(prefix);
    if (!end.empty()) {
        d.AddMember("range_end", Value(base64Encode(end).c_str(), alloc), alloc);
    }

    std::string resp = post("/v3/kv/range", stringify(d));
    if (resp.empty() || hasError(resp)) return {};

    std::vector<std::pair<std::string, std::string>> result;
    Document r;
    r.Parse(resp.c_str());
    if (r.HasParseError()) return result;

    if (r.HasMember("kvs") && r["kvs"].IsArray()) {
        for (auto& kv : r["kvs"].GetArray()) {
            std::string k = base64Decode(kv["key"].GetString());
            std::string v = base64Decode(kv["value"].GetString());
            result.emplace_back(std::move(k), std::move(v));
        }
    }
    return result;
}

} // namespace discovery
