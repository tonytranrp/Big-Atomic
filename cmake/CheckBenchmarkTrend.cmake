if(NOT DEFINED BENCH_EXE)
  message(FATAL_ERROR "BENCH_EXE is required")
endif()
if(NOT DEFINED OUTPUT_JSON)
  message(FATAL_ERROR "OUTPUT_JSON is required")
endif()
if(NOT DEFINED CORE_COUNT)
  message(FATAL_ERROR "CORE_COUNT is required")
endif()
if(NOT DEFINED SUMMARY_TXT)
  message(FATAL_ERROR "SUMMARY_TXT is required")
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

set(BA_GE_FOUND OFF)
set(BA_GE_THREADS 2147483647)
set(BA_GE_REAL_TIME "")
set(BA_GE_ITEMS_PER_SEC "")
set(BA_MAX_THREADS 0)
set(BA_MAX_REAL_TIME "")
set(BA_MAX_ITEMS_PER_SEC "")

set(SL_GE_FOUND OFF)
set(SL_GE_THREADS 2147483647)
set(SL_GE_REAL_TIME "")
set(SL_GE_ITEMS_PER_SEC "")
set(SL_MAX_THREADS 0)
set(SL_MAX_REAL_TIME "")
set(SL_MAX_ITEMS_PER_SEC "")

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
  string(JSON BA_IPS GET "${BA_BENCH_JSON}" benchmarks ${BA_I} items_per_second)

  if(BA_NAME MATCHES "^BM_BigAtomic_Mixed")
    if(BA_THREADS_FOUND GREATER BA_MAX_THREADS)
      set(BA_MAX_THREADS "${BA_THREADS_FOUND}")
      set(BA_MAX_REAL_TIME "${BA_TIME}")
      set(BA_MAX_ITEMS_PER_SEC "${BA_IPS}")
    endif()

    if(BA_THREADS_FOUND GREATER_EQUAL CORE_COUNT)
      if((NOT BA_GE_FOUND) OR (BA_THREADS_FOUND LESS BA_GE_THREADS))
        set(BA_GE_FOUND ON)
        set(BA_GE_THREADS "${BA_THREADS_FOUND}")
        set(BA_GE_REAL_TIME "${BA_TIME}")
        set(BA_GE_ITEMS_PER_SEC "${BA_IPS}")
      endif()
    endif()
  endif()

  if(BA_NAME MATCHES "^BM_SeqLock_Mixed")
    if(BA_THREADS_FOUND GREATER SL_MAX_THREADS)
      set(SL_MAX_THREADS "${BA_THREADS_FOUND}")
      set(SL_MAX_REAL_TIME "${BA_TIME}")
      set(SL_MAX_ITEMS_PER_SEC "${BA_IPS}")
    endif()

    if(BA_THREADS_FOUND GREATER_EQUAL CORE_COUNT)
      if((NOT SL_GE_FOUND) OR (BA_THREADS_FOUND LESS SL_GE_THREADS))
        set(SL_GE_FOUND ON)
        set(SL_GE_THREADS "${BA_THREADS_FOUND}")
        set(SL_GE_REAL_TIME "${BA_TIME}")
        set(SL_GE_ITEMS_PER_SEC "${BA_IPS}")
      endif()
    endif()
  endif()
endforeach()

if(BA_GE_FOUND)
  set(BA_THREADS "${BA_GE_THREADS}")
  set(BA_REAL_TIME "${BA_GE_REAL_TIME}")
  set(BA_ITEMS_PER_SEC "${BA_GE_ITEMS_PER_SEC}")
  set(BA_SELECTION "first_ge_core")
else()
  set(BA_THREADS "${BA_MAX_THREADS}")
  set(BA_REAL_TIME "${BA_MAX_REAL_TIME}")
  set(BA_ITEMS_PER_SEC "${BA_MAX_ITEMS_PER_SEC}")
  set(BA_SELECTION "max_fallback")
endif()

if(SL_GE_FOUND)
  set(SL_THREADS "${SL_GE_THREADS}")
  set(SL_REAL_TIME "${SL_GE_REAL_TIME}")
  set(SL_ITEMS_PER_SEC "${SL_GE_ITEMS_PER_SEC}")
  set(SL_SELECTION "first_ge_core")
else()
  set(SL_THREADS "${SL_MAX_THREADS}")
  set(SL_REAL_TIME "${SL_MAX_REAL_TIME}")
  set(SL_ITEMS_PER_SEC "${SL_MAX_ITEMS_PER_SEC}")
  set(SL_SELECTION "max_fallback")
endif()

if(BA_REAL_TIME STREQUAL "" OR SL_REAL_TIME STREQUAL "")
  message(FATAL_ERROR "Failed to locate BM_BigAtomic_Mixed and BM_SeqLock_Mixed data rows.")
endif()

message(STATUS "Trend gate target core count: ${CORE_COUNT}")
message(STATUS "BigAtomic selected row: threads=${BA_THREADS}, policy=${BA_SELECTION}, real_time=${BA_REAL_TIME}")
message(STATUS "SeqLock selected row: threads=${SL_THREADS}, policy=${SL_SELECTION}, real_time=${SL_REAL_TIME}")

if(BA_REAL_TIME LESS SL_REAL_TIME)
  set(GATE_RESULT "PASS")
  message(STATUS "[M1 GATE PASS] BigAtomic real_time < SeqLock real_time at selected contention row.")
else()
  set(GATE_RESULT "FAIL")
  file(WRITE "${SUMMARY_TXT}"
    "M1 Benchmark Trend Summary\n"
    "core_count=${CORE_COUNT}\n"
    "row_policy=first_ge_core_else_max\n"
    "big_atomic.selection=${BA_SELECTION}\n"
    "big_atomic.threads=${BA_THREADS}\n"
    "big_atomic.real_time=${BA_REAL_TIME}\n"
    "big_atomic.items_per_second=${BA_ITEMS_PER_SEC}\n"
    "seqlock.selection=${SL_SELECTION}\n"
    "seqlock.threads=${SL_THREADS}\n"
    "seqlock.real_time=${SL_REAL_TIME}\n"
    "seqlock.items_per_second=${SL_ITEMS_PER_SEC}\n"
    "gate_result=${GATE_RESULT}\n")
  message(FATAL_ERROR
    "[M1 GATE FAIL] BigAtomic real_time (${BA_REAL_TIME}) >= SeqLock real_time (${SL_REAL_TIME}) "
    "at selected contention row."
  )
endif()

file(WRITE "${SUMMARY_TXT}"
  "M1 Benchmark Trend Summary\n"
  "core_count=${CORE_COUNT}\n"
  "row_policy=first_ge_core_else_max\n"
  "big_atomic.selection=${BA_SELECTION}\n"
  "big_atomic.threads=${BA_THREADS}\n"
  "big_atomic.real_time=${BA_REAL_TIME}\n"
  "big_atomic.items_per_second=${BA_ITEMS_PER_SEC}\n"
  "seqlock.selection=${SL_SELECTION}\n"
  "seqlock.threads=${SL_THREADS}\n"
  "seqlock.real_time=${SL_REAL_TIME}\n"
  "seqlock.items_per_second=${SL_ITEMS_PER_SEC}\n"
  "gate_result=${GATE_RESULT}\n")
