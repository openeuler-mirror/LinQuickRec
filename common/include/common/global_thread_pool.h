#ifndef COMMON_GLOBAL_THREAD_POOL_H
#define COMMON_GLOBAL_THREAD_POOL_H

#include "thread_pool.h"
#include <memory>
#include <gflags/gflags.h>

DECLARE_int32(global_thread_pool_size);

namespace common {

/**
 * @brief 获取全局线程池单例
 * 
 * @return ThreadPool& 全局线程池实例
 */
ThreadPool& get_global_thread_pool();

} // namespace common

#endif // COMMON_GLOBAL_THREAD_POOL_H
