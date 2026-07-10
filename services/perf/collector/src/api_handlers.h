#ifndef PERF_COLLECTOR_API_HANDLERS_H
#define PERF_COLLECTOR_API_HANDLERS_H

#include <brpc/controller.h>
#include <brpc/server.h>

#include <sstream>
#include <string>
#include <vector>

#include "api_handlers.pb.h"
#include "series_manager.h"
#include "sqlite_store.h"
#include "stats_engine.h"

namespace perf {

class ApiHandlerService : public ApiHandler {
public:
    explicit ApiHandlerService(SqliteStore* store)
        : sqlite_store_(store) {}

    void Handle(google::protobuf::RpcController* controller,
                const ApiRequest*,
                ApiResponse*,
                google::protobuf::Closure* done) override;

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
