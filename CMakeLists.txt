cmake_minimum_required(VERSION 3.24)
include(FetchContent)
project(ref_ptr)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(test)
target_sources(test PRIVATE test.cpp)

option(BUILD_BENCHMARK "build benchmark using google benchmark" ON)

if(BUILD_BENCHMARK)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git

  GIT_TAG        bc946b919cac6f25a199a526da571638cfde109f
  FIND_PACKAGE_ARGS NAMES benchmark
)

set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(concurrency_bench)
target_sources(concurrency_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/concurrency_bench.cpp)
target_link_libraries(concurrency_bench PRIVATE benchmark::benchmark)

endif()
