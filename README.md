# Big Atomics

This repository contains a C++20 implementation of the `Cached-Memory-Efficient`
Big Atomic algorithm and follow-on M2 work from:

- Daniel Anderson, Guy Blelloch, Siddhartha Jayanti
- "Big Atomics"
- arXiv: [2501.07503](https://arxiv.org/abs/2501.07503)
- ACM DOI: [10.1145/3710848.3710874](https://doi.org/10.1145/3710848.3710874)

## Scope

Current implementation focuses on:

- Algorithm 2 (`Cached-Memory-Efficient`) BigAtomic
- Pool-based node reclamation
- correctness tests, benchmark trend gate, repeat tearing gate
- `CacheHash<K,V>` built on `BigAtomic<Entry>`

Deferred:

- Algorithm 1 / Algorithm 3
- SMR policy abstraction

## Build Prerequisites

- CMake 3.21+
- Visual Studio x64 toolchain (Windows path)
- WSL2 + GCC for TSan path
- Internet access during first configure (CPM downloads Catch2 + benchmark)

## Configure / Build / Test

Debug:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset debug
ctest --preset debug-tests
```

Release (includes benchmark trend gate + repeat tearing gate):

```powershell
cmake --preset windows-msvc-release
cmake --build --preset release
ctest --preset release-tests
```

WSL2 TSan:

```bash
cmake --preset wsl2-gcc-tsan
cmake --build --preset wsl2-tsan
ctest --preset wsl2-tsan-tests
```

## Concurrency Gates

- `BA_TEARING_REPEAT_COUNT` (default `200`) controls release repeat tearing gate.
- benchmark trend gate writes:
- `build/.../m1_bench_results.json`
- `build/.../m1_bench_summary.txt`
- tracing is centralized in `include/big_atomics/detail/trace.hpp`.
- `BA_TRACE_CONCURRENCY` is enabled in Debug + RelWithDebInfo, disabled in Release.
- any PR touching `include/big_atomics/` or `src/` should pass MSVC Debug/Release plus WSL2 TSan tests before merge.

## CacheHash Constraints

`CacheHash<K,V>` enforces:

- `K` and `V` are trivially copyable/destructible and standard-layout
- `K` and `V` have unique object representations
- internal `Entry` satisfies `BigAtomicSafe<Entry>` via explicit layout/padding
