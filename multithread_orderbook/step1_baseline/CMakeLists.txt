cmake_minimum_required(VERSION 3.10)
project(step1_baseline)

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

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orderbook PRIVATE -Wall -Wextra -Wpedantic)
endif()

