#include "common/global_thread_pool.h"
#include <thread>

DEFINE_int32(global_thread_pool_size, 128, "全局线程池大小（默认 128）");

namespace common {

ThreadPool& get_global_thread_pool() {
    static std::unique_ptr<ThreadPool> instance;
    static std::once_flag flag;
    
    std::call_once(flag, []() {
        int pool_size = FLAGS_global_thread_pool_size;
        if (pool_size <= 0) {
            int cpu_cores = std::thread::hardware_concurrency();
            pool_size = std::max(4, cpu_cores * 2);
        }
        instance = std::make_unique<ThreadPool>(pool_size);
    });
    
    return *instance;
}

} // namespace common
