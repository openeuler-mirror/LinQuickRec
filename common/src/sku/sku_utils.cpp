#include "common/sku_utils.h"

#include <functional>
#include <sstream>

#define COMMON_LOGGER_COMPAT_MODE
#include "common/logger.h"

namespace common {

std::vector<uint64_t> parse_skus_from_string(const std::string& skus) {
    std::vector<uint64_t> sku_ids;

    if (skus.empty()) {
        LOG(WARNING) << "Empty skus string";
        return sku_ids;
    }

    size_t pos = 0;

    while (pos + SKU_ID_LENGTH <= skus.size()) {
        std::string sku_str = skus.substr(pos, SKU_ID_LENGTH);

        try {
            uint64_t sku_id = std::stoull(sku_str);
            sku_ids.push_back(sku_id);
        } catch (const std::exception& e) {
            LOG(WARNING) << "Failed to parse SKU ID: " << sku_str
                        << ", error: " << e.what();
        }

        pos += SKU_ID_LENGTH;
    }

    LOG(INFO) << "Parsed " << sku_ids.size() << " SKU IDs from string";
    return sku_ids;
}

std::string skus_to_string(const std::vector<uint64_t>& sku_ids) {
    std::string result;
    for (uint64_t sku_id : sku_ids) {
        char buffer[8];
        snprintf(buffer, sizeof(buffer), "%06lu", sku_id);
        result += buffer;
    }
    return result;
}

std::map<int, std::vector<uint64_t>> distribute_skus_by_hash(
    const std::vector<uint64_t>& sku_ids,
    int n_workers) {

    std::map<int, std::vector<uint64_t>> distribution;

    for (int i = 0; i < n_workers; ++i) {
        distribution[i] = std::vector<uint64_t>();
    }

    for (uint64_t sku_id : sku_ids) {
        size_t hash = std::hash<uint64_t>{}(sku_id);
        int worker_index = hash % n_workers;
        distribution[worker_index].push_back(sku_id);
    }

    for (int i = 0; i < n_workers; ++i) {
        LOG(INFO) << "Worker " << i << " assigned " << distribution[i].size() << " SKUs";
    }

    return distribution;
}

} // namespace common
