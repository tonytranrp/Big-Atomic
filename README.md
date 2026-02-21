# Big Atomics M1

This repository contains a C++20 implementation of the `Cached-Memory-Efficient`
Big Atomic algorithm from:

- Daniel Anderson, Guy Blelloch, Siddhartha Jayanti
- "Big Atomics"
- arXiv: [2501.07503](https://arxiv.org/abs/2501.07503)
- ACM DOI: [10.1145/3710848.3710874](https://doi.org/10.1145/3710848.3710874)

## Scope (M1)

This milestone intentionally implements:

- Algorithm 2 (`Cached-Memory-Efficient`) only
- `load`, `store`, and `CAS` for wide trivially copyable structs
- A pool-only reclamation path (`NodePool<T,3>`)
- Unit tests plus a SeqLock comparison benchmark and trend gate

This milestone intentionally excludes:

- Algorithm 1 / Algorithm 3
- CacheHash or other data structures built on top
- Packaging and install/export polish

## Build Prerequisites

- CMake 3.21+
- Visual Studio 2022 (x64 toolchain)
- Internet access during first configure (CPM downloads Catch2 + benchmark)

## Configure / Build / Test

Debug:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset debug
ctest --preset debug-tests
```

Release (includes benchmark trend gate):

```powershell
cmake --preset windows-msvc-release
cmake --build --preset release
ctest --preset release-tests
```

## Notes

- `BigAtomic<T>` enforces strict compile-time constraints in
  `include/big_atomics/detail/constraints.hpp`.
- `tagged_null` encodes a sentinel pointer and masks the version to 32 bits
  to keep pointer patterns canonical-safe on x64 sanitizer/debug builds.
