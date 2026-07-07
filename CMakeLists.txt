cmake_minimum_required(VERSION 3.15)
project(MicroLatencyValidator LANGUAGES CXX)

# Enforce C++20 standard for std::span and [[likely]]/[[unlikely]] branch predictors
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# HARDWARE SYMPATHY COMPILER FLAGS
# -O3: Maximum speed optimization.
# -march=native: Generates optimal assembly for the host CPU architecture.
# -Wall -Wextra -pedantic: Strict warnings. In HFT, warnings are treated as errors.
# ==============================================================================
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -pedantic")

# Tell CMake where to find our core, net, trading, and utils headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# Find the threading library for the host OS (pthread wrapper)
find_package(Threads REQUIRED)

# ==============================================================================
# TARGET 1: The Tick-to-Trade Validator (The Trading Firm)
# ==============================================================================
add_executable(validator src/main.cpp)
target_link_libraries(validator PRIVATE Threads::Threads)

# ==============================================================================
# TARGET 2: The Exchange Simulator (The Market)
# ==============================================================================
add_executable(exchange src/data_generator.cpp)
# The data generator doesn't strictly need threads, but we link it for consistency
target_link_libraries(exchange PRIVATE Threads::Threads)