cmake_minimum_required(VERSION 3.21)
project(BigAtomics_M1 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BA_ENABLE_COMPILE_TESTS "Enable configure-time negative compile tests" ON)
set(BA_TEARING_REPEAT_COUNT "200" CACHE STRING "Repeat count for release tearing gate")
set(BA_TEARING_ITERATION_TIMEOUT_SECONDS "30" CACHE STRING "Per-iteration timeout for repeat tearing gate")

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
    "BENCHMARK_ENABLE_WERROR OFF"
)

add_library(big_atomics INTERFACE)
target_include_directories(
  big_atomics INTERFACE
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
target_compile_features(big_atomics INTERFACE cxx_std_20)
target_compile_definitions(
  big_atomics INTERFACE
  $<$<CONFIG:Debug>:BA_TRACE_CONCURRENCY=1>
  $<$<CONFIG:RelWithDebInfo>:BA_TRACE_CONCURRENCY=1>
  $<$<CONFIG:Release>:BA_TRACE_CONCURRENCY=0>
)

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
  ba_cache_hash_test
  src/test_cache_hash.cpp
)
target_link_libraries(ba_cache_hash_test PRIVATE big_atomics Catch2::Catch2WithMain)

add_executable(
  ba_bench
  src/bench_seqlock_vs_bigatomic.cpp
)
target_link_libraries(ba_bench PRIVATE big_atomics benchmark::benchmark)

add_executable(
  ba_cache_hash_bench
  src/bench_cache_hash.cpp
)
target_link_libraries(ba_cache_hash_bench PRIVATE big_atomics benchmark::benchmark)

include(CTest)
enable_testing()

if(Catch2_ADDED)
  list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()
include(Catch)
catch_discover_tests(ba_test)
catch_discover_tests(ba_cache_hash_test)

cmake_host_system_information(RESULT BA_PHYSICAL_CORES QUERY NUMBER_OF_PHYSICAL_CORES)
if(NOT BA_PHYSICAL_CORES OR BA_PHYSICAL_CORES STREQUAL "0")
  set(BA_PHYSICAL_CORES 1)
endif()

set(BA_BENCH_JSON "${CMAKE_BINARY_DIR}/m1_bench_results.json")
set(BA_BENCH_SUMMARY "${CMAKE_BINARY_DIR}/m1_bench_summary.txt")
set(BA_ADD_RELEASE_GATES OFF)
if(CMAKE_CONFIGURATION_TYPES)
  set(BA_ADD_RELEASE_GATES ON)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(BA_ADD_RELEASE_GATES ON)
endif()

if(BA_ADD_RELEASE_GATES)
  add_test(
    NAME M1_BenchTrendGate
    COMMAND "${CMAKE_COMMAND}"
      "-DBENCH_EXE=$<TARGET_FILE:ba_bench>"
      "-DOUTPUT_JSON=${BA_BENCH_JSON}"
      "-DSUMMARY_TXT=${BA_BENCH_SUMMARY}"
      "-DCORE_COUNT=${BA_PHYSICAL_CORES}"
      -P "${CMAKE_SOURCE_DIR}/cmake/CheckBenchmarkTrend.cmake"
  )
  set_tests_properties(
    M1_BenchTrendGate
    PROPERTIES
      LABELS "M1;Benchmark;Trend"
      TIMEOUT 300
  )

  add_test(
    NAME M2_RepeatTearingGate
    COMMAND "${CMAKE_COMMAND}"
      "-DCTEST_BIN=${CMAKE_CTEST_COMMAND}"
      "-DTEST_DIR=${CMAKE_BINARY_DIR}"
      "-DREPEAT_COUNT=${BA_TEARING_REPEAT_COUNT}"
      "-DITERATION_TIMEOUT_SECONDS=${BA_TEARING_ITERATION_TIMEOUT_SECONDS}"
      -P "${CMAKE_SOURCE_DIR}/cmake/RunRepeatTearing.cmake"
  )
  set_tests_properties(
    M2_RepeatTearingGate
    PROPERTIES
      RUN_SERIAL TRUE
      LABELS "M2;Correctness;Tearing"
      TIMEOUT 900
  )

  if(CMAKE_CONFIGURATION_TYPES)
    set_tests_properties(
      M1_BenchTrendGate M2_RepeatTearingGate
      PROPERTIES
        CONFIGURATIONS Release
    )
  endif()
endif()
