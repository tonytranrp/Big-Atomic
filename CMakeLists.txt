cmake_minimum_required(VERSION 3.21)
project(BigAtomics_M1 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BA_ENABLE_COMPILE_TESTS "Enable configure-time negative compile tests" ON)

# CPM bootstrap
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
  file(
    DOWNLOAD
    "https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/CPM.cmake"
    "${CPM_DOWNLOAD_LOCATION}"
    TLS_VERIFY ON
  )
endif()
include("${CPM_DOWNLOAD_LOCATION}")

# Dependencies
CPMAddPackage("gh:catchorg/Catch2@3.5.2")
CPMAddPackage(
  NAME benchmark
  GITHUB_REPOSITORY google/benchmark
  GIT_TAG v1.8.4
  OPTIONS
    "BENCHMARK_ENABLE_TESTING OFF"
    "BENCHMARK_ENABLE_INSTALL OFF"
    "BENCHMARK_ENABLE_GTEST_TESTS OFF"
)

add_library(big_atomics INTERFACE)
target_include_directories(
  big_atomics INTERFACE
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
target_compile_features(big_atomics INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(
    big_atomics INTERFACE
    /W4
    /WX
    /wd4324
    /permissive-
    /Zc:__cplusplus
    /EHsc
    /arch:AVX2
  )
else()
  target_compile_options(
    big_atomics INTERFACE
    -Wall
    -Wextra
    -Werror
    -mcx16
  )
endif()

if(BA_ENABLE_COMPILE_TESTS)
  include("${CMAKE_SOURCE_DIR}/cmake/CompileTests.cmake")
endif()

add_executable(
  ba_test
  src/test_correctness.cpp
)
target_link_libraries(ba_test PRIVATE big_atomics Catch2::Catch2WithMain)

add_executable(
  ba_bench
  src/bench_seqlock_vs_bigatomic.cpp
)
target_link_libraries(ba_bench PRIVATE big_atomics benchmark::benchmark)

include(CTest)
enable_testing()

if(Catch2_ADDED)
  list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()
include(Catch)
catch_discover_tests(ba_test)

cmake_host_system_information(RESULT BA_PHYSICAL_CORES QUERY NUMBER_OF_PHYSICAL_CORES)
if(NOT BA_PHYSICAL_CORES OR BA_PHYSICAL_CORES STREQUAL "0")
  set(BA_PHYSICAL_CORES 1)
endif()

set(BA_BENCH_JSON "${CMAKE_BINARY_DIR}/m1_bench_results.json")
add_test(
  NAME M1_BenchTrendGate
  COMMAND "${CMAKE_COMMAND}"
    "-DBENCH_EXE=$<TARGET_FILE:ba_bench>"
    "-DOUTPUT_JSON=${BA_BENCH_JSON}"
    "-DCORE_COUNT=${BA_PHYSICAL_CORES}"
    -P "${CMAKE_SOURCE_DIR}/cmake/CheckBenchmarkTrend.cmake"
)
set_tests_properties(
  M1_BenchTrendGate
  PROPERTIES
    CONFIGURATIONS Release
    LABELS "M1;Benchmark;Trend"
    TIMEOUT 300
)
