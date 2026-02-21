# Development Log - Big Atomics (Fresh Test Report)

## Date
- 2026-02-21

## Scope
- Replaced previous `devlog.md` content.
- This file now contains only the latest bounded test run results and scores.

## Bounded Execution Policy Used
- Every `ctest` run used:
  - `--stop-on-failure`
  - explicit `--timeout`
- External wall-clock caps were also used while invoking commands.
- Any leftover test processes were checked/stopped before and after runs.

## Test Scoreboard (Latest Run)

### 1) Debug correctness full suite
- Command:
  - `ctest --test-dir build/gcc-debug --output-on-failure --stop-on-failure --timeout 120`
- Score:
  - **10/10 passed**
- Result:
  - PASS
- Total real time:
  - **12.33 sec**

### 2) Release correctness subset
- Command:
  - `ctest --test-dir build/gcc-release -R "Paper API|std compare_exchange updates expected on failure|std exchange returns previous value|Concurrent non-tearing reads|Reclaim stress under contention|CacheHash" --output-on-failure --stop-on-failure --timeout 120`
- Score:
  - **10/10 passed**
- Result:
  - PASS
- Total real time:
  - **1.93 sec**

### 3) Release repeat tearing gate
- Command:
  - `ctest --test-dir build/gcc-release -R "M2_RepeatTearingGate" --output-on-failure --stop-on-failure --timeout 180`
- Score:
  - **0/1 passed**
- Result:
  - FAIL
- Total real time:
  - **33.15 sec**
- Failure detail:
  - Gate failed at **iteration 9/25**.
  - Inner test timeout:
    - `Concurrent non-tearing reads` timed out at **30.02 sec**.

### 4) Release trend gate
- Command:
  - `ctest --test-dir build/gcc-release -R "M1_BenchTrendGate" --output-on-failure --stop-on-failure --timeout 180`
- Score:
  - **1/1 passed**
- Result:
  - PASS
- Total real time:
  - **4.89 sec**

## Trend Artifact (Latest)
- File:
  - `build/gcc-release/m1_bench_summary.txt`
- Parsed values:
  - `core_count=10`
  - `row_policy=first_ge_core_else_max`
  - `big_atomic.threads=16`
  - `big_atomic.real_time=5.6654953524431253`
  - `big_atomic.items_per_second=176507072.69025841`
  - `seqlock.threads=16`
  - `seqlock.real_time=87.232678818955122`
  - `seqlock.items_per_second=11463593.845093591`
  - `gate_result=PASS`

## Overall Status From This Batch
- Debug correctness: PASS
- Release correctness subset: PASS
- Release trend gate: PASS
- Release repeat tearing gate: **FAIL (timeout flake in iteration loop)**

## Notes
- This report reflects only the newest run batch.
- No previous log sections were retained.
