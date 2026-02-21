if(NOT DEFINED CTEST_BIN)
  message(FATAL_ERROR "CTEST_BIN is required")
endif()

if(NOT DEFINED TEST_DIR)
  message(FATAL_ERROR "TEST_DIR is required")
endif()

if(NOT DEFINED REPEAT_COUNT)
  message(FATAL_ERROR "REPEAT_COUNT is required")
endif()

if(REPEAT_COUNT LESS 1)
  message(FATAL_ERROR "REPEAT_COUNT must be >= 1")
endif()

if(NOT DEFINED ITERATION_TIMEOUT_SECONDS)
  set(ITERATION_TIMEOUT_SECONDS 10)
endif()

if(ITERATION_TIMEOUT_SECONDS LESS 1)
  message(FATAL_ERROR "ITERATION_TIMEOUT_SECONDS must be >= 1")
endif()

math(EXPR LAST_ITERATION "${REPEAT_COUNT}")
math(EXPR CTEST_LAUNCH_TIMEOUT "${ITERATION_TIMEOUT_SECONDS} + 5")
foreach(ITER RANGE 1 ${LAST_ITERATION})
  execute_process(
    COMMAND
      "${CTEST_BIN}"
      "--test-dir" "${TEST_DIR}"
      "-R" "Concurrent non-tearing reads"
      "--output-on-failure"
      "--stop-on-failure"
      "--timeout" "${ITERATION_TIMEOUT_SECONDS}"
    TIMEOUT "${CTEST_LAUNCH_TIMEOUT}"
    RESULT_VARIABLE TEST_EXIT
    OUTPUT_VARIABLE TEST_STDOUT
    ERROR_VARIABLE TEST_STDERR
  )

  if(NOT TEST_EXIT EQUAL 0)
    set(FAIL_REPORT
      "Repeat tearing gate failed at iteration ${ITER}/${REPEAT_COUNT}\n"
      "iteration_timeout_seconds=${ITERATION_TIMEOUT_SECONDS}\n"
      "=== STDOUT ===\n${TEST_STDOUT}\n"
      "=== STDERR ===\n${TEST_STDERR}\n")
    message(FATAL_ERROR "${FAIL_REPORT}")
  endif()
endforeach()

message(STATUS "Repeat tearing gate passed: ${REPEAT_COUNT}/${REPEAT_COUNT}")
