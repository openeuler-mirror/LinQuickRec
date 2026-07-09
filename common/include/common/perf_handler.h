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
// Dummy protobuf message for HTTP-only service that never serializes protos.
// GetRequestPrototype/GetResponsePrototype are required by the Service
// interface but never called in HTTP mode.
class DummyMessage : public google::protobuf::Message {
    google::protobuf::Message* New(google::protobuf::Arena*) const override { return nullptr; }
    const google::protobuf::Descriptor* GetDescriptor() const override { return nullptr; }
    const google::protobuf::Reflection* GetReflection() const override { return nullptr; }
    void CopyFrom(const google::protobuf::Message&) override {}
    void MergeFrom(const google::protobuf::Message&) override {}
    void Clear() override {}
    bool IsInitialized() const override { return true; }
    void MergePartialFromCodedStream(google::protobuf::io::CodedInputStream*) override {}
    size_t ByteSizeLong() const override { return 0; }
    int GetCachedSize() const override { return 0; }
    uint8_t* _InternalSerialize(uint8_t*, google::protobuf::io::EpsCopyOutputStream*) const override { return nullptr; }
};

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
            cntl->response_attachment().append(R"({"error":"perf ring not initialized"})");
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
        cntl->response_attachment().append(body);

        done->Run();
    }

    const google::protobuf::Message& GetRequestPrototype(
        const google::protobuf::MethodDescriptor*) const override {
        static DummyMessage empty;
        return empty;
    }

    const google::protobuf::Message& GetResponsePrototype(
        const google::protobuf::MethodDescriptor*) const override {
        static DummyMessage empty;
        return empty;
    }
};

} // namespace perf
} // namespace common

#endif // COMMON_PERF_HANDLER_H
