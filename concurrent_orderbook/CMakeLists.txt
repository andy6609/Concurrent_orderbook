cmake_minimum_required(VERSION 3.10)
project(concurrent_orderbook VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# OrderBook library
add_library(orderbook
    src/order_book.cpp
)

# Test executable
add_executable(test_correctness
    tests/test_correctness.cpp
)
target_link_libraries(test_correctness orderbook)

# Benchmark executable
add_executable(bench_comparison
    benchmarks/bench_comparison.cpp
)
target_link_libraries(bench_comparison orderbook pthread)

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orderbook PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optimization for benchmarks
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(orderbook PRIVATE -O3)
endif()

