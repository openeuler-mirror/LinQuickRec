#ifndef COMMON_PERF_HANDLER_H
#define COMMON_PERF_HANDLER_H

#include <brpc/controller.h>
#include <brpc/server.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>

#include "common/perf_registry.h"

namespace common {
namespace perf {

// Minimal BRPC service that handles /debug/perf requests.
// Register via server.AddService(new DebugPerfService, brpc::SERVER_OWNS_SERVICE).
class DebugPerfService : public google::protobuf::Service {
public:
    const google::protobuf::ServiceDescriptor* GetDescriptor() override {
        return nullptr;
    }

    void CallMethod(const google::protobuf::MethodDescriptor*,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message*,
                    google::protobuf::Message*,
                    google::protobuf::Closure* done) override {
        auto* cntl = static_cast<brpc::Controller*>(controller);
        auto& registry = PerfRingRegistry::Instance();
        if (!registry.Initialized()) {
            cntl->http_response().set_status_code(503);
            cntl->http_response().set_content_type("application/json");
            cntl->http_response().body() = R"({"error":"perf ring not initialized"})";
            done->Run();
            return;
        }

        auto snapshot = registry.TakeAll();

        cntl->http_response().set_status_code(200);
        cntl->http_response().set_content_type("application/json");

        std::string body = "{";
        body += R"("dropped":)" + std::to_string(snapshot.dropped) + ",";
        body += R"("spans":[)";
        for (size_t i = 0; i < snapshot.spans.size(); ++i) {
            if (i > 0) body += ",";
            auto& s = snapshot.spans[i];
            body += "{";
            body += R"("ts_us":)" + std::to_string(s.ts_us) + ",";
            body += R"("service":")" + std::string(s.service) + "\",";
            body += R"("stage":")" + std::string(s.stage) + "\",";
            body += R"("metric":")" + std::string(s.metric) + "\",";
            body += R"("trace_id":")" + std::string(s.trace_id) + "\",";
            body += R"("duration_ms":)" + std::to_string(s.duration_ms) + ",";
            body += R"("status":")" + std::string(s.status) + "\"";
            body += "}";
        }
        body += "]}";
        cntl->http_response().body() = body;

        done->Run();
    }

    const google::protobuf::Message* GetRequestPrototype(
        const google::protobuf::MethodDescriptor*) override {
        return nullptr;
    }

    const google::protobuf::Message* GetResponsePrototype(
        const google::protobuf::MethodDescriptor*) override {
        return nullptr;
    }
};

} // namespace perf
} // namespace common

#endif // COMMON_PERF_HANDLER_H
