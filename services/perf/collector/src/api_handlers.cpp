#include "api_handlers.h"

#include <brpc/controller.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "common/logger.h"
#include "series_manager.h"
#include "stats_engine.h"

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
    } else if (uri.find("/api/v1/series") != std::string::npos) {
        HandleSeries(cntl);
    } else if (uri.find("/api/v1/outliers") != std::string::npos) {
        HandleOutliers(cntl);
    } else if (uri.find("/js/") != std::string::npos || uri == "/" || uri.empty()) {
        std::string path = (uri == "/" || uri.empty()) ? "/index.html" : uri;
        HandleStatic(cntl, path);
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

    auto spans = sqlite_store_->QueryTrace(trace_id);

    std::ostringstream body;
    body << R"({"trace_id":")" << trace_id << R"(","spans":[)";
    for (size_t i = 0; i < spans.size(); ++i) {
        if (i > 0) body << ",";
        auto& s = spans[i];
        body << R"({"service":")" << s.service << "\""
             << R"(,"stage":")" << s.stage << "\""
             << R"(,"duration_ms":)" << s.duration_ms
             << R"(,"ts_us":)" << s.ts_us
             << R"(,"status":")" << s.status << "\"}";
    }
    body << "]}";
    JsonOk(cntl, body.str());
}

void ApiHandlerService::HandleSeries(brpc::Controller* cntl) {
    const std::string& uri = cntl->http_request().uri();

    if (uri.find("/start") != std::string::npos) {
        std::string name = ExtractParam(uri, "name");
        if (name.empty()) name = "unnamed";
        int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t id = SeriesManager::Instance().Start(name, now_us);
        std::ostringstream body;
        body << R"({"id":)" << id << R"(,"name":")" << name << R"(","status":"active"})";
        JsonOk(cntl, body.str());
    } else if (uri.find("/stop") != std::string::npos) {
        int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        SeriesManager::Instance().Stop(now_us);
        JsonOk(cntl, R"({"status":"stopped"})");
    } else {
        // List all series
        std::ostringstream body;
        body << R"({"series":[)";
        auto list = SeriesManager::Instance().List();
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) body << ",";
            body << R"({"id":)" << list[i].id
                 << R"(,"name":")" << list[i].name << "\""
                 << R"(,"span_count":)" << list[i].span_count
                 << R"(,"status":")" << (list[i].active ? "active" : "stopped") << R"("})";
        }
        body << "]}";
        JsonOk(cntl, body.str());
    }
}

void ApiHandlerService::HandleOutliers(brpc::Controller* cntl) {
    const std::string& uri = cntl->http_request().uri();
    std::string stage = ExtractParam(uri, "stage");

    auto* st = StatsEngine::Instance().Get(
        ExtractParam(uri, "service"), stage);
    if (!st) {
        JsonError(cntl, 404, "no data for stage");
        return;
    }

    std::ostringstream body;
    body << R"({"stage":")" << stage << "\""
         << R"(,"avg":)" << st->Avg()
         << R"(,"stddev":)" << st->StdDev()
         << R"(,"outliers":[])";
    body << "}";
    JsonOk(cntl, body.str());
}

void ApiHandlerService::HandleStatic(brpc::Controller* cntl, const std::string& path) {
    std::string file_path = "/app/webui" + path;
    std::ifstream file(file_path);
    if (!file.is_open()) {
        cntl->http_response().set_status_code(404);
        cntl->http_response().body() = "Not found";
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    cntl->http_response().set_status_code(200);
    if (path.find(".js") != std::string::npos) {
        cntl->http_response().set_content_type("application/javascript");
    } else if (path.find(".html") != std::string::npos) {
        cntl->http_response().set_content_type("text/html; charset=utf-8");
    } else {
        cntl->http_response().set_content_type("text/plain");
    }
    cntl->http_response().body() = content;
}

void ApiHandlerService::HandleNotFound(brpc::Controller* cntl) {
    JsonError(cntl, 404, "not found");
}

} // namespace perf
