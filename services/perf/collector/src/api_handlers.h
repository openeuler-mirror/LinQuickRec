#ifndef PERF_COLLECTOR_API_HANDLERS_H
#define PERF_COLLECTOR_API_HANDLERS_H

#include <brpc/controller.h>
#include <brpc/server.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>

#include <sstream>
#include <string>
#include <vector>

#include "series_manager.h"
#include "sqlite_store.h"
#include "stats_engine.h"

namespace perf {

class ApiHandlerService : public google::protobuf::Service {
public:
    explicit ApiHandlerService(SqliteStore* store)
        : sqlite_store_(store) {}

    const google::protobuf::ServiceDescriptor* GetDescriptor() override {
        return nullptr;
    }

    void CallMethod(const google::protobuf::MethodDescriptor*,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message*,
                    google::protobuf::Message*,
                    google::protobuf::Closure* done) override;

    const google::protobuf::Message* GetRequestPrototype(
        const google::protobuf::MethodDescriptor*) override { return nullptr; }
    const google::protobuf::Message* GetResponsePrototype(
        const google::protobuf::MethodDescriptor*) override { return nullptr; }

private:
    void HandleHealth(brpc::Controller* cntl);
    void HandleStatsCurrent(brpc::Controller* cntl);
    void HandleTrace(brpc::Controller* cntl);
    void HandleSeries(brpc::Controller* cntl);
    void HandleOutliers(brpc::Controller* cntl);
    void HandleStatic(brpc::Controller* cntl, const std::string& path);
    void HandleNotFound(brpc::Controller* cntl);

    SqliteStore* sqlite_store_;
};

} // namespace perf

#endif // PERF_COLLECTOR_API_HANDLERS_H
