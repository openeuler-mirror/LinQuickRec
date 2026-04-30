#ifndef COMMON_RANDOM_UTILS_H
#define COMMON_RANDOM_UTILS_H

#include <cstddef>
#include <string>

namespace common {

/**
 * @brief 生成随机可打印 ASCII 字符串（字符范围 32-126）
 * @param size_bytes 字符串长度（字节数）
 */
std::string generate_random_string(size_t size_bytes);

/**
 * @brief 生成随机纯数字字符串（字符范围 '0'-'9'）
 * @param size_bytes 字符串长度（字节数）
 */
std::string generate_random_numeric_string(size_t size_bytes);

} // namespace common

#endif // COMMON_RANDOM_UTILS_H
