if(NOT DEFINED BENCH_EXE)
  message(FATAL_ERROR "BENCH_EXE is required")
endif()
if(NOT DEFINED OUTPUT_JSON)
  message(FATAL_ERROR "OUTPUT_JSON is required")
endif()
if(NOT DEFINED CORE_COUNT)
  message(FATAL_ERROR "CORE_COUNT is required")
endif()

execute_process(
  COMMAND "${BENCH_EXE}"
    --benchmark_format=json
    --benchmark_out=${OUTPUT_JSON}
    --benchmark_min_time=0.15s
  RESULT_VARIABLE BA_BENCH_RESULT
)
if(NOT BA_BENCH_RESULT EQUAL 0)
  message(FATAL_ERROR "Benchmark execution failed with code ${BA_BENCH_RESULT}")
endif()

file(READ "${OUTPUT_JSON}" BA_BENCH_JSON)
string(JSON BA_BENCHMARK_COUNT LENGTH "${BA_BENCH_JSON}" benchmarks)
if(NOT BA_BENCHMARK_COUNT GREATER 0)
  message(FATAL_ERROR "No benchmark rows found in ${OUTPUT_JSON}")
endif()

set(BA_BEST_DIST 2147483647)
set(SL_BEST_DIST 2147483647)
set(BA_REAL_TIME "")
set(SL_REAL_TIME "")
set(BA_THREADS "")
set(SL_THREADS "")

math(EXPR BA_LAST_INDEX "${BA_BENCHMARK_COUNT} - 1")
foreach(BA_I RANGE 0 ${BA_LAST_INDEX})
  string(JSON BA_NAME GET "${BA_BENCH_JSON}" benchmarks ${BA_I} name)

  if(BA_NAME MATCHES "_mean$" OR BA_NAME MATCHES "_median$" OR BA_NAME MATCHES "_stddev$" OR BA_NAME MATCHES "_cv$")
    continue()
  endif()

  if(NOT (BA_NAME MATCHES "^BM_BigAtomic_Mixed" OR BA_NAME MATCHES "^BM_SeqLock_Mixed"))
    continue()
  endif()

  string(REGEX MATCH "threads:([0-9]+)" BA_MATCH "${BA_NAME}")
  if(NOT CMAKE_MATCH_1)
    continue()
  endif()
  set(BA_THREADS_FOUND "${CMAKE_MATCH_1}")

  string(JSON BA_TIME GET "${BA_BENCH_JSON}" benchmarks ${BA_I} real_time)
  math(EXPR BA_DIST "${BA_THREADS_FOUND} - ${CORE_COUNT}")
  if(BA_DIST LESS 0)
    math(EXPR BA_DIST "-${BA_DIST}")
  endif()

  if(BA_NAME MATCHES "^BM_BigAtomic_Mixed")
    if(BA_DIST LESS BA_BEST_DIST)
      set(BA_BEST_DIST ${BA_DIST})
      set(BA_REAL_TIME "${BA_TIME}")
      set(BA_THREADS "${BA_THREADS_FOUND}")
    endif()
  endif()

  if(BA_NAME MATCHES "^BM_SeqLock_Mixed")
    if(BA_DIST LESS SL_BEST_DIST)
      set(SL_BEST_DIST ${BA_DIST})
      set(SL_REAL_TIME "${BA_TIME}")
      set(SL_THREADS "${BA_THREADS_FOUND}")
    endif()
  endif()
endforeach()

if(BA_REAL_TIME STREQUAL "" OR SL_REAL_TIME STREQUAL "")
  message(FATAL_ERROR "Failed to locate BM_BigAtomic_Mixed and BM_SeqLock_Mixed data rows.")
endif()

message(STATUS "Trend gate target core count: ${CORE_COUNT}")
message(STATUS "BigAtomic nearest row: threads=${BA_THREADS}, real_time=${BA_REAL_TIME}")
message(STATUS "SeqLock nearest row: threads=${SL_THREADS}, real_time=${SL_REAL_TIME}")

if(BA_REAL_TIME LESS SL_REAL_TIME)
  message(STATUS "[M1 GATE PASS] BigAtomic real_time < SeqLock real_time near contention target.")
else()
  message(FATAL_ERROR
    "[M1 GATE FAIL] BigAtomic real_time (${BA_REAL_TIME}) >= SeqLock real_time (${SL_REAL_TIME}) "
    "near the target thread count."
  )
endif()
