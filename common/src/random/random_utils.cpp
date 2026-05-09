#include "common/random_utils.h"

#include <random>
#include <string>

namespace common {

std::string generate_random_string(size_t size_bytes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(32, 126);

    std::string result;
    result.resize(size_bytes);
    for (size_t i = 0; i < size_bytes; ++i) {
        result[i] = static_cast<char>(dis(gen));
    }
    return result;
}

std::string generate_random_numeric_string(size_t size_bytes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis('0', '9');

    std::string result;
    result.resize(size_bytes);
    for (size_t i = 0; i < size_bytes; ++i) {
        result[i] = static_cast<char>(dis(gen));
    }
    return result;
}

} // namespace common
