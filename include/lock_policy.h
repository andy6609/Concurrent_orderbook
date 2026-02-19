#pragma once
#include <mutex>
#include <shared_mutex>

struct MutexPolicy {
    using mutex_type = std::mutex;
    using read_lock  = std::unique_lock<std::mutex>;
    using write_lock = std::unique_lock<std::mutex>;
};

struct SharedMutexPolicy {
    using mutex_type = std::shared_mutex;
    using read_lock  = std::shared_lock<std::shared_mutex>;
    using write_lock = std::unique_lock<std::shared_mutex>;
};
