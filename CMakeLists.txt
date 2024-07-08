cmake_minimum_required(VERSION 3.24)
include(FetchContent)
project(ref_ptr)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(ref_ptr INTERFACE)
add_library(ref_ptr::ref_ptr ALIAS ref_ptr)
target_include_directories(ref_ptr INTERFACE include)

target_include_directories(ref_ptr INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

option(REF_PTR_BUILD_BENCHMARK "build benchmark using google benchmark" OFF)
option(REF_PTR_BUILD_TEST "build test using google test" OFF)


if(REF_PTR_BUILD_BENCHMARK)
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
target_link_libraries(concurrency_bench PRIVATE benchmark::benchmark ref_ptr::ref_ptr)
target_include_directories(concurrency_bench PRIVATE example utils)

endif()

if(REF_PTR_BUILD_TEST)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        f8d7d77c06936315286eb55f8de22cd23c188571 # tag-1.14.0
  FIND_PACKAGE_ARGS NAMES GTest
)

FetchContent_MakeAvailable(googletest)


add_executable(test ${CMAKE_CURRENT_SOURCE_DIR}/test/test.cpp)
target_link_libraries(test GTest::gtest GTest::gtest_main GTest::gmock
                        GTest::gmock_main ref_ptr::ref_ptr)
target_include_directories(test PRIVATE example utils)
include(GoogleTest)
gtest_discover_tests(test)

endif()

