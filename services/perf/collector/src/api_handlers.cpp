#include "api_handlers.h"

#include <brpc/controller.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <iomanip>
#include <sstream>

#include "common/logger.h"

namespace perf {

using namespace rapidjson;

namespace {

void JsonOk(brpc::Controller* cntl, const std::string& body) {
    cntl->http_response().set_status_code(200);
    cntl->http_response().set_content_type("application/json");
    cntl->http_response().body() = body;
}

void JsonError(brpc::Controller* cntl, int code, const std::string& msg) {
    cntl->http_response().set_status_code(code);
    cntl->http_response().set_content_type("application/json");
    std::string body = R"({"error":")" + msg + R"("})";
    cntl->http_response().body() = body;
}

std::string StatsToJson(const WelfordRunningStats& st) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << R"({"avg":)" << st.Avg()
        << R"(,"min":)" << st.Min()
        << R"(,"max":)" << st.Max()
        << R"(,"p50":)" << st.P50()
        << R"(,"p99":)" << st.P99()
        << R"(,"stddev":)" << st.StdDev()
        << R"(,"count":)" << st.Count()
        << "}";
    return oss.str();
}

std::string ExtractParam(const std::string& uri, const std::string& key) {
    std::string pattern = key + "=";
    size_t pos = uri.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.size();
    size_t end = uri.find('&', pos);
    if (end == std::string::npos) end = uri.size();
    return uri.substr(pos, end - pos);
}

} // namespace

void ApiHandlerService::CallMethod(
    const google::protobuf::MethodDescriptor*,
    google::protobuf::RpcController* controller,
    const google::protobuf::Message*,
    google::protobuf::Message*,
    google::protobuf::Closure* done) {

    auto* cntl = static_cast<brpc::Controller*>(controller);
    const std::string& uri = cntl->http_request().uri();

    if (uri.find("/api/v1/health") != std::string::npos) {
        HandleHealth(cntl);
    } else if (uri.find("/api/v1/stats/current") != std::string::npos) {
        HandleStatsCurrent(cntl);
    } else if (uri.find("/api/v1/trace/") != std::string::npos) {
        HandleTrace(cntl);
    } else {
        HandleNotFound(cntl);
    }

    done->Run();
}

void ApiHandlerService::HandleHealth(brpc::Controller* cntl) {
    JsonOk(cntl, R"({"status":"ok"})");
}

void ApiHandlerService::HandleStatsCurrent(brpc::Controller* cntl) {
    const std::string& uri = cntl->http_request().uri();
    std::string service = ExtractParam(uri, "service");
    std::string stage = ExtractParam(uri, "stage");

    if (service.empty() || stage.empty()) {
        JsonError(cntl, 400, "missing service or stage param");
        return;
    }

    auto* st = StatsEngine::Instance().Get(service, stage);
    if (!st) {
        JsonError(cntl, 404, "no data for " + service + "|" + stage);
        return;
    }

    JsonOk(cntl, StatsToJson(*st));
}

void ApiHandlerService::HandleTrace(brpc::Controller* cntl) {
    const std::string& uri = cntl->http_request().uri();
    size_t pos = uri.rfind('/');
    if (pos == std::string::npos) {
        JsonError(cntl, 400, "missing trace_id");
        return;
    }
    std::string trace_id = uri.substr(pos + 1);

    // Query SQLite for this trace
    // Phase 2.2: implement SQL query
    std::ostringstream body;
    body << R"({"trace_id":")" << trace_id << R"(","spans":[]})";
    JsonOk(cntl, body.str());
}

void ApiHandlerService::HandleNotFound(brpc::Controller* cntl) {
    JsonError(cntl, 404, "not found");
}

} // namespace perf
