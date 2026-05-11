#ifndef COMMON_SKU_UTILS_H
#define COMMON_SKU_UTILS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace common {

constexpr size_t SKU_ID_LENGTH = 6;

std::vector<uint64_t> parse_skus_from_string(const std::string& skus);

std::string skus_to_string(const std::vector<uint64_t>& sku_ids);

std::map<int, std::vector<uint64_t>> distribute_skus_by_hash(
    const std::vector<uint64_t>& sku_ids, int n_workers);

} // namespace common

#endif // COMMON_SKU_UTILS_H
