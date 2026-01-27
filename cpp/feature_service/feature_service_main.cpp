//
// Created by lixu on 2026/1/3.
//

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <json2pb/pb_to_json.h>

#include "kr_user_log_redis.h"
#include "kr_user_feature_redis.h"
#include "feature_service.h"

DEFINE_int32(port, 8000, "TCP Port of the feature service server");
DEFINE_string(listen_addr, "", "Server listen address, may be IPV4/IPV6/UDS."
" If this is set, the flag port will be ignored");


int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    brpc::Server server;
    UserFeatureRedis uf;
    UserLogRedis ul;
    LinQuickRec::FeatureServiceImpl feature_service_impl(&uf, &ul);
    if (server.AddService(&feature_service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    butil::EndPoint point;
    if (!FLAGS_listen_addr.empty()) {
        if (butil::str2endpoint(FLAGS_listen_addr.c_str(), &point) < 0) {
            LOG(ERROR) << "Invalid listen address:" << FLAGS_listen_addr;
            return -1;
        }
    } else {
        point = butil::EndPoint(butil::IP_ANY, FLAGS_port);
    }

    // Start the server.
    brpc::ServerOptions options;
    if (server.Start(point, &options) != 0) {
        LOG(ERROR) << "Fail to start FeatureServiceServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
