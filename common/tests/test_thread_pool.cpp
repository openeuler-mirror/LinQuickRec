#include "common/thread_pool.h"
#include <iostream>
#include <cassert>
#include <atomic>
#include <chrono>

int main() {
    std::cout << "=== Test ThreadPool ===" << std::endl;

    // 1. Create thread pool and check size
    {
        common::ThreadPool pool(4);
        assert(pool.size() == 4);
        assert(pool.pending_tasks() == 0);
        std::cout << "[PASS] Created pool with " << pool.size() << " threads" << std::endl;
    }

    // 2. Submit a task and get result
    {
        common::ThreadPool pool(2);
        auto future = pool.submit([](int a, int b) { return a + b; }, 3, 4);
        int result = future.get();
        assert(result == 7);
        std::cout << "[PASS] Task result: " << result << std::endl;
    }

    // 3. Multiple concurrent tasks
    {
        common::ThreadPool pool(4);
        std::atomic<int> counter{0};
        constexpr int TASK_COUNT = 100;

        std::vector<std::future<void>> futures;
        for (int i = 0; i < TASK_COUNT; ++i) {
            futures.push_back(pool.submit([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        }
        for (auto& f : futures) {
            f.get();
        }
        assert(counter == TASK_COUNT);
        std::cout << "[PASS] " << TASK_COUNT << " concurrent tasks completed" << std::endl;
    }

    // 4. Tasks with return values
    {
        common::ThreadPool pool(3);
        std::vector<std::future<int>> futures;
        for (int i = 0; i < 10; ++i) {
            futures.push_back(pool.submit([i]() { return i * i; }));
        }
        int sum = 0;
        for (auto& f : futures) {
            sum += f.get();
        }
        assert(sum == 285); // sum of squares 0..9
        std::cout << "[PASS] Sum of squares: " << sum << std::endl;
    }

    // 5. Pending tasks count
    {
        common::ThreadPool pool(1);
        pool.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
        pool.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
        // At this point, second task should be pending
        assert(pool.pending_tasks() >= 1);
        pool.submit([]() {});
        std::cout << "[PASS] Pending tasks tracking" << std::endl;
    }

    // 6. Exception safety: submit after destruction is not allowed
    // (Pool destructor waits for tasks, so this is safe by design)
    {
        common::ThreadPool pool(2);
        auto future = pool.submit([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return 42;
        });
        assert(future.get() == 42);
        std::cout << "[PASS] Task completion before destruction" << std::endl;
    }

    std::cout << "\n=== All ThreadPool Tests Passed ===" << std::endl;
    return 0;
}
