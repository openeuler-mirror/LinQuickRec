#include "common/global_thread_pool.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <iomanip>

struct TaskResult {
    int id;
    long long result;
    long long exec_ns;
    long long wait_ns;
};

static long long fib(int n) {
    long long a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        long long c = a + b;
        a = b;
        b = c;
    }
    return n <= 1 ? n : b;
}

static void print_percentile(const std::vector<long long>& data, const std::string& label, double p) {
    size_t idx = static_cast<size_t>(data.size() * p / 100.0);
    if (idx >= data.size()) idx = data.size() - 1;
    std::cout << "  " << label << std::setw(8) << data[idx] << std::endl;
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    common::ThreadPool& pool = common::get_global_thread_pool();
    const int N = 100;

    std::vector<std::future<TaskResult>> futures;
    futures.reserve(N);

    auto wall_start = std::chrono::steady_clock::now();

    for (int i = 0; i < N; ++i) {
        auto t_submit = std::chrono::steady_clock::now();
        futures.push_back(pool.submit([i, t_submit]() -> TaskResult {
            auto t_exec_start = std::chrono::steady_clock::now();
            long long wait_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                t_exec_start - t_submit).count();
            int n = 30 + (i % 13);
            long long r = fib(n);
            auto t_exec_end = std::chrono::steady_clock::now();
            long long exec_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                t_exec_end - t_exec_start).count();
            return {i, r, exec_ns, wait_ns};
        }));
    }

    std::vector<long long> exec_times, wait_times;
    exec_times.reserve(N);
    wait_times.reserve(N);

    for (auto& f : futures) {
        auto tr = f.get();
        exec_times.push_back(tr.exec_ns);
        wait_times.push_back(tr.wait_ns);
    }

    long long wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - wall_start).count();

    long long total_exec = std::accumulate(exec_times.begin(), exec_times.end(), 0LL);
    long long total_wait = std::accumulate(wait_times.begin(), wait_times.end(), 0LL);
    double avg_exec = static_cast<double>(total_exec) / N;
    double avg_wait = static_cast<double>(total_wait) / N;

    std::sort(exec_times.begin(), exec_times.end());
    std::sort(wait_times.begin(), wait_times.end());

    std::cout << "=== Thread Pool Fibonacci Benchmark ===" << std::endl;
    std::cout << "thread pool size: " << pool.size() << std::endl;
    std::cout << "tasks submitted:  " << N << std::endl;
    std::cout << "input range:      fib(30) ~ fib(42)" << std::endl;
    std::cout << std::endl;

    std::cout << "--- execution time (ns) ---" << std::endl;
    std::cout << "  min:   " << std::setw(10) << exec_times.front() << std::endl;
    std::cout << "  max:   " << std::setw(10) << exec_times.back() << std::endl;
    std::cout << "  avg:   " << std::setw(10) << static_cast<long long>(avg_exec) << std::endl;
    print_percentile(exec_times, "p50:   ", 50);
    print_percentile(exec_times, "p90:   ", 90);
    print_percentile(exec_times, "p99:   ", 99);
    print_percentile(exec_times, "p100:  ", 100);
    std::cout << "  total: " << std::setw(10) << total_exec << std::endl;
    std::cout << std::endl;

    std::cout << "--- queue wait time (ns) ---" << std::endl;
    std::cout << "  min:   " << std::setw(10) << wait_times.front() << std::endl;
    std::cout << "  max:   " << std::setw(10) << wait_times.back() << std::endl;
    std::cout << "  avg:   " << std::setw(10) << static_cast<long long>(avg_wait) << std::endl;
    print_percentile(wait_times, "p50:   ", 50);
    print_percentile(wait_times, "p90:   ", 90);
    print_percentile(wait_times, "p99:   ", 99);
    print_percentile(wait_times, "p100:  ", 100);
    std::cout << "  total: " << std::setw(10) << total_wait << std::endl;
    std::cout << std::endl;

    double wall_ms = static_cast<double>(wall_ns) / 1e6;
    std::cout << "total wall time: " << wall_ns << " ns (" << std::fixed
              << std::setprecision(2) << wall_ms << " ms)" << std::endl;

    return 0;
}
