cmake_minimum_required(VERSION 3.10)
project(step2_coarse_lock)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# OrderBook library
add_library(thread_safe_book
    src/thread_safe_book.cpp
)

# Benchmark executable
add_executable(bench_contention
    benchmarks/bench_contention.cpp
)
target_link_libraries(bench_contention thread_safe_book pthread)

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(thread_safe_book PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optimization for benchmarks
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(thread_safe_book PRIVATE -O3)
endif()

