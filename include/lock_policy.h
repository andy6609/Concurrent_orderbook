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

// All instances using this policy serialize on one process-wide mutex. It is
// useful as a controlled baseline when comparing one global lock with sharded
// per-instance locks.
struct ProcessWideMutex {
    void lock() { mutex_.lock(); }
    bool try_lock() { return mutex_.try_lock(); }
    void unlock() { mutex_.unlock(); }

private:
    inline static std::mutex mutex_;
};

struct ProcessWideMutexPolicy {
    using mutex_type = ProcessWideMutex;
    using read_lock  = std::unique_lock<ProcessWideMutex>;
    using write_lock = std::unique_lock<ProcessWideMutex>;
};
