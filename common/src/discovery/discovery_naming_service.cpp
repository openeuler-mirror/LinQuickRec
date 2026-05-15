#include "common/discovery_naming_service.h"

#include <bthread/bthread.h>
#include <butil/endpoint.h>

#include "common/logger.h"

namespace common {

// ---- gflags ----

DEFINE_int32(discovery_naming_timeout_ms, 3000,
             "Discovery naming service RPC timeout (ms)");
DEFINE_string(discovery_naming_connection_type, "single",
              "Discovery naming service channel connection type");
DEFINE_int32(discovery_naming_max_retry, 2,
             "Discovery naming service channel max retry");
DEFINE_int32(discovery_naming_connect_timeout_ms, -1,
             "Discovery naming service channel connect timeout (ms), -1 = disabled");
DEFINE_int32(discovery_naming_backup_request_ms, -1,
             "Discovery naming service channel backup request (ms), -1 = disabled");

// ---- DiscoveryCache ----

DiscoveryCache* DiscoveryCache::instance() {
    static DiscoveryCache cache;
    return &cache;
}

DiscoveryCache::DiscoveryCache() {
    brpc::ChannelOptions opts;
    opts.timeout_ms = FLAGS_discovery_naming_timeout_ms;
    opts.connection_type = FLAGS_discovery_naming_connection_type.c_str();
    opts.max_retry = FLAGS_discovery_naming_max_retry;
    if (FLAGS_discovery_naming_connect_timeout_ms >= 0) {
        opts.connect_timeout_ms = FLAGS_discovery_naming_connect_timeout_ms;
    }
    if (FLAGS_discovery_naming_backup_request_ms >= 0) {
        opts.backup_request_ms = FLAGS_discovery_naming_backup_request_ms;
    }

    LOG_INFO << "DiscoveryCache: connecting to " << FLAGS_discovery_addr
             << " (timeout=" << FLAGS_discovery_naming_timeout_ms
             << "ms, connection_type=" << FLAGS_discovery_naming_connection_type << ")";
    if (discovery_channel_.Init(FLAGS_discovery_addr.c_str(), &opts) != 0) {
        LOG_ERROR << "DiscoveryNamingService: failed to init channel to "
                  << FLAGS_discovery_addr;
        return;
    }

    stub_ = std::make_unique<discovery::DiscoveryService_Stub>(&discovery_channel_);
    LOG_INFO << "DiscoveryNamingService connected to " << FLAGS_discovery_addr;
}

int DiscoveryCache::QueryDiscovery(const std::string& service_name,
                                   std::vector<brpc::ServerNode>* servers) {
    if (!stub_) {
        LOG_ERROR << "DiscoveryNamingService: stub not initialized";
        return -1;
    }

    discovery::DiscoverRequest req;
    req.set_service_name(service_name);

    discovery::DiscoverResponse rsp;
    brpc::Controller cntl;

    stub_->Discover(&cntl, &req, &rsp, nullptr);

    if (cntl.Failed()) {
        LOG_ERROR << "DiscoveryNamingService: Discover(" << service_name
                  << ") failed: " << cntl.ErrorText();
        return -1;
    }

    servers->clear();
    for (const auto& inst : rsp.instances()) {
        if (inst.status() == discovery::InstanceStatus::UP) {
            butil::EndPoint ep;
            std::string addr = inst.host() + ":" + std::to_string(inst.port());
            if (butil::str2endpoint(addr.c_str(), &ep) == 0) {
                servers->push_back(brpc::ServerNode(ep));
            }
        }
    }

    LOG_INFO << "DiscoveryNamingService: Discover(" << service_name
             << ") returned " << servers->size() << " instance(s)";
    return 0;
}

// ---- DiscoveryNamingService ----

int DiscoveryNamingService::RunNamingService(
    const char* service_name,
    brpc::NamingServiceActions* actions) {

    while (true) {
        std::vector<brpc::ServerNode> servers;
        int ret = DiscoveryCache::instance()->QueryDiscovery(service_name, &servers);
        if (ret == 0) {
            actions->ResetServers(servers);
        }

        int rc = bthread_usleep(
            static_cast<int64_t>(FLAGS_discovery_refresh_interval_ms) * 1000);
        if (rc != 0) {
            break;
        }
    }
    return 0;
}

brpc::NamingService* DiscoveryNamingService::New() const {
    return new DiscoveryNamingService();
}

void DiscoveryNamingService::Destroy() {
    delete this;
}

// ---- Register with BRPC ----

namespace {
struct DiscoveryNSRegistrar {
    DiscoveryNSRegistrar() {
        brpc::NamingServiceExtension()->Register("discovery",
                                                  new DiscoveryNamingService());
    }
};
static DiscoveryNSRegistrar s_discovery_ns_registrar;
} // anonymous namespace

} // namespace common
