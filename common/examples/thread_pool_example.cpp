#include "common/global_thread_pool.h"
#include <iostream>
#include <chrono>

static void SimulateTask(int id, int sleep_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    std::cout << "  Task " << id << " done (" << sleep_ms << "ms)" << std::endl;
}

static int ComputeFibonacci(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(int argc, char* argv[]) {
    gflags::SetUsageMessage("Thread pool usage example");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    common::ThreadPool& pool = common::get_global_thread_pool();

    std::cout << "Global thread pool size: " << pool.size() << std::endl;
    std::cout << "Pending tasks: " << pool.pending_tasks() << std::endl;
    std::cout << std::endl;

    // Example 1: Fire-and-forget tasks (void return)
    std::cout << "1. Submit fire-and-forget tasks:" << std::endl;
    for (int i = 0; i < 4; ++i) {
        pool.submit(SimulateTask, i, (i + 1) * 200);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << std::endl;

    // Example 2: Tasks with return values
    std::cout << "2. Submit tasks and collect results (Fibonacci):" << std::endl;
    auto f1 = pool.submit(ComputeFibonacci, 10);  // 55
    auto f2 = pool.submit(ComputeFibonacci, 15);  // 610
    auto f3 = pool.submit(ComputeFibonacci, 20);  // 6765

    std::cout << "  fib(10)=" << f1.get() << std::endl;
    std::cout << "  fib(15)=" << f2.get() << std::endl;
    std::cout << "  fib(20)=" << f3.get() << std::endl;
    std::cout << std::endl;

    // Example 3: Lambda tasks
    std::cout << "3. Submit lambda tasks:" << std::endl;
    int a = 40, b = 2;
    auto lam = pool.submit([a, b]() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return a / b;
    });
    std::cout << "  40/2=" << lam.get() << std::endl;
    std::cout << std::endl;

    // Example 4: Mixed load
    std::cout << "4. Mixed workload:" << std::endl;
    for (int i = 0; i < 6; ++i) {
        pool.submit([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::cout << "  Mixed task " << i << " completed" << std::endl;
        });
    }
    std::cout << "  Pending tasks after submit: " << pool.pending_tasks() << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "  Pending tasks after drain: " << pool.pending_tasks() << std::endl;

    std::cout << std::endl << "All examples completed." << std::endl;
    return 0;
}
