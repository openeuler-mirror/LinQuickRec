#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "common/logger.h"
#include "common/service_discovery.h"

namespace perf {

void StartPuller(
    std::shared_ptr<common::ServiceDiscovery> discovery,
    const std::string& sqlite_db_path) {

    LOG_INFO << "Puller started, discovery backend: "
             << discovery << ", db: " << sqlite_db_path;

    // Phase 1.2: Implement BRPC pull loop
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace perf
