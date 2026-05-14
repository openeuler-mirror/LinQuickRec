#include "discovery_naming_service.h"

#include <chrono>

#include <brpc/channel.h>
#include <butil/endpoint.h>

#include "common/logger.h"

namespace proxy {

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

    if (discovery_channel_.Init(FLAGS_discovery_addr.c_str(), &opts) != 0) {
        LOG_ERROR << "DiscoveryNamingService: failed to init channel to "
                  << FLAGS_discovery_addr;
        return;
    }

    stub_ = std::make_unique<discovery::DiscoveryService_Stub>(&discovery_channel_);
    LOG_INFO << "DiscoveryNamingService connected to " << FLAGS_discovery_addr;

    refresh_thread_ = std::thread(&DiscoveryCache::RefreshLoop, this);
}

DiscoveryCache::~DiscoveryCache() {
    running_ = false;
    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }
}

int DiscoveryCache::GetServers(const std::string& service_name,
                               std::vector<brpc::ServerNode>* servers) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(service_name);
        if (it != cache_.end()) {
            *servers = it->second;
            return servers->empty() ? -1 : 0;
        }
    }

    // Cache miss — synchronously query Discovery
    std::vector<brpc::ServerNode> nodes;
    int ret = QueryDiscovery(service_name, &nodes);
    if (ret < 0) {
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[service_name] = nodes;
    }

    *servers = std::move(nodes);
    return servers->empty() ? -1 : 0;
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

void DiscoveryCache::RefreshLoop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(FLAGS_discovery_refresh_interval_ms));

        if (!running_) break;

        // Copy service names under lock, then refresh without holding the lock
        std::vector<std::string> services;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& kv : cache_) {
                services.push_back(kv.first);
            }
        }

        for (const auto& svc : services) {
            if (!running_) break;

            std::vector<brpc::ServerNode> nodes;
            if (QueryDiscovery(svc, &nodes) < 0) continue;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                int old_size = static_cast<int>(cache_[svc].size());
                cache_[svc] = nodes;
                if (static_cast<int>(nodes.size()) != old_size) {
                    LOG_INFO << "DiscoveryNamingService: " << svc
                             << " instances: " << old_size
                             << " -> " << nodes.size();
                }
            }
        }
    }
}

// ---- DiscoveryNamingService ----

int DiscoveryNamingService::GetServers(const char* service_name,
                                       std::vector<brpc::ServerNode>* servers) {
    return DiscoveryCache::instance()->GetServers(service_name, servers);
}

void DiscoveryNamingService::Describe(std::ostream& os,
                                      const brpc::ServerId& id) const {
    os << "discovery://" << id.tag;
}

// ---- Register with BRPC ----

static brpc::NamingServiceRegisterer s_discovery_ns_reg(
    "discovery",
    []() -> brpc::NamingService* {
        return new proxy::DiscoveryNamingService();
    });

} // namespace proxy
