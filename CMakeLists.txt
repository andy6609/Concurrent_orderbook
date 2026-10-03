cmake_minimum_required(VERSION 3.10)
project(concurrent_orderbook VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# OrderBook library
add_library(orderbook
    src/order_book.cpp
    src/order_pool.cpp
    src/sharded_order_book.cpp
)

# Test executable
add_executable(test_correctness
    tests/test_correctness.cpp
)
target_link_libraries(test_correctness orderbook pthread)

# Benchmark executables
add_executable(bench_comparison
    benchmarks/bench_comparison.cpp
)
target_link_libraries(bench_comparison orderbook pthread)

add_executable(bench_pool
    benchmarks/bench_pool.cpp
)
target_link_libraries(bench_pool orderbook pthread)

add_executable(bench_sharding
    benchmarks/bench_sharding.cpp
)
target_link_libraries(bench_sharding orderbook pthread)

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orderbook PRIVATE -Wall -Wextra -Wpedantic)
    # The correctness suite currently uses assert(); keep checks active even
    # when the test executable is built with -DCMAKE_BUILD_TYPE=Release.
    target_compile_options(test_correctness PRIVATE -UNDEBUG)
elseif(MSVC)
    target_compile_options(test_correctness PRIVATE /UNDEBUG)
endif()

# Optimization for benchmarks
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(orderbook PRIVATE -O3)
endif()
