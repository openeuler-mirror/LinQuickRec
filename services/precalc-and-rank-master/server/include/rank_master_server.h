#ifndef PRECALC_AND_RANK_MASTER_RANK_MASTER_SERVER_H
#define PRECALC_AND_RANK_MASTER_RANK_MASTER_SERVER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "common/service_discovery.h"
#include "common/error.h"
#include "common/logger.h"
#include <datasystem/datasystem.h>
#include "rank_master.pb.h"
#include "rank_sub.pb.h"

DECLARE_int32(rank_master_server_port);
DECLARE_string(registry_backend);
DECLARE_string(discovery_addr);
DECLARE_string(etcd_endpoints);
DECLARE_int32(sub_worker_timeout_ms);
DECLARE_string(sub_worker_service_type);
DECLARE_string(sub_worker_connection_type);
DECLARE_int32(sub_worker_max_retry);
DECLARE_int32(sub_worker_connect_timeout_ms);
DECLARE_int32(sub_worker_backup_request_ms);
DECLARE_int32(rank_master_sleep_time_ms);

namespace rank {

class RankMasterServiceImpl : public RankMasterService {
public:
    RankMasterServiceImpl();

    bool IsReady() const { return ready_; }

    void Rank(google::protobuf::RpcController* controller,
              const RankMasterRequest* request,
              RankMasterResponse* response,
              google::protobuf::Closure* done) override;

private:
    common::error::Status process_rank_request(const RankMasterRequest* request,
                                               RankMasterResponse* response);

    bool call_sub_worker(const std::string& user_feat_data,
                         const std::vector<uint64_t>& sku_ids,
                         const std::string& trace_id,
                         RankSubResponse* response);

    std::unique_ptr<common::ServiceDiscovery> service_discovery_;
    std::shared_ptr<datasystem::ServiceDiscovery> kv_service_discovery_;
    bool ready_ = true;
};

} // namespace rank

#endif
