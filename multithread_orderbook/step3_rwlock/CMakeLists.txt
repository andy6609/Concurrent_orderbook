cmake_minimum_required(VERSION 3.10)
project(step3_fine_lock)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# OrderBook library
add_library(fine_grained_book
    src/fine_grained_book.cpp
)

# Benchmark executable
add_executable(bench_scaling
    benchmarks/bench_scaling.cpp
)
target_link_libraries(bench_scaling fine_grained_book pthread)

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fine_grained_book PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optimization for benchmarks
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(fine_grained_book PRIVATE -O3)
endif()

