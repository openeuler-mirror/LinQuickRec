// Precalc 服务单元测试
// 测试 common::generate_random_string 和 common::generate_random_numeric_string

#include "common/random_utils.h"
#include <iostream>
#include <cassert>
#include <string>

// ============================================================================
// generate_random_string 测试
// ============================================================================

void test_generate_random_string_size() {
    for (size_t size : {0, 1, 100, 1024, 4096}) {
        std::string result = common::generate_random_string(size);
        assert(result.size() == size);
    }
    std::cout << "[PASS] generate_random_string: correct sizes" << std::endl;
}

void test_generate_random_string_printable() {
    std::string result = common::generate_random_string(1000);
    for (char c : result) {
        assert(static_cast<int>(c) >= 32 && static_cast<int>(c) <= 126);
    }
    std::cout << "[PASS] generate_random_string: all chars printable ASCII" << std::endl;
}

void test_generate_random_string_different() {
    std::string a = common::generate_random_string(1024);
    std::string b = common::generate_random_string(1024);
    assert(a != b);
    std::cout << "[PASS] generate_random_string: different calls produce different results" << std::endl;
}

void test_generate_random_string_mb() {
    std::string result = common::generate_random_string(1.0 * 1024 * 1024);
    assert(result.size() == 1024 * 1024);
    std::cout << "[PASS] generate_random_string: 1 MB size correct" << std::endl;
}

// ============================================================================
// generate_random_numeric_string 测试
// ============================================================================

void test_generate_random_numeric_string_size() {
    for (size_t size : {0, 1, 16, 100, 1024}) {
        std::string result = common::generate_random_numeric_string(size);
        assert(result.size() == size);
    }
    std::cout << "[PASS] generate_random_numeric_string: correct sizes" << std::endl;
}

void test_generate_random_numeric_string_digits_only() {
    std::string result = common::generate_random_numeric_string(1000);
    for (char c : result) {
        assert(c >= '0' && c <= '9');
    }
    std::cout << "[PASS] generate_random_numeric_string: all chars are digits" << std::endl;
}

void test_generate_random_numeric_string_different() {
    std::string a = common::generate_random_numeric_string(1024);
    std::string b = common::generate_random_numeric_string(1024);
    assert(a != b);
    std::cout << "[PASS] generate_random_numeric_string: different calls produce different results" << std::endl;
}

// ============================================================================
// main
// ============================================================================

int main() {
    std::cout << "=== Random Utils Unit Tests ===" << std::endl;

    // generate_random_string
    test_generate_random_string_size();
    test_generate_random_string_printable();
    test_generate_random_string_different();
    test_generate_random_string_mb();

    // generate_random_numeric_string
    test_generate_random_numeric_string_size();
    test_generate_random_numeric_string_digits_only();
    test_generate_random_numeric_string_different();

    std::cout << "\n=== All Random Utils Tests Passed ===" << std::endl;
    return 0;
}
