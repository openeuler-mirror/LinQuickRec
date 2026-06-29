#ifndef RANK_SUB_SERVER_H
#define RANK_SUB_SERVER_H

#include <cstdint>
#include <string>
#include <vector>

#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include "common/error.h"
#include "common/logger.h"
#include "rank_sub.pb.h"

DECLARE_int32(server_port);
DECLARE_int32(rank_sub_sleep_time_ms);

namespace rank {

double simulate_score(uint64_t sku_id, const std::string& user_feat);

class RankSubServiceImpl : public RankSubService {
public:
    RankSubServiceImpl();

    void Rank(google::protobuf::RpcController* controller,
              const RankSubRequest* request,
              RankSubResponse* response,
              google::protobuf::Closure* done) override;

private:
    common::error::Status process_rank_request(const RankSubRequest* request,
                                               RankSubResponse* response);
};

} // namespace rank

#endif
