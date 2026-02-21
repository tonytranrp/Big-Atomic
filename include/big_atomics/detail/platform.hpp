#pragma once

#include <atomic>
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace ba::detail::platform {

static_assert(
  std::atomic_ref<std::uint64_t>::is_always_lock_free,
  "[BigAtomic] std::atomic_ref<uint64_t> must be lock-free on this platform."
);

static_assert(
  std::atomic<void*>::is_always_lock_free,
  "[BigAtomic] Pointer-sized atomics must be lock-free on this platform."
);

inline void compiler_fence() noexcept {
#if defined(_MSC_VER)
  _ReadWriteBarrier();
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

inline void full_fence() noexcept {
#if defined(_MSC_VER)
  _ReadWriteBarrier();
  std::atomic_thread_fence(std::memory_order_seq_cst);
  _ReadWriteBarrier();
#elif defined(__GNUC__) || defined(__clang__)
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
  std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}

inline void before_bytewise_load() noexcept {
  compiler_fence();
}

inline void after_bytewise_load() noexcept {
  compiler_fence();
}

inline void before_bytewise_store() noexcept {
  compiler_fence();
}

inline void after_bytewise_store() noexcept {
  compiler_fence();
}

inline void protect_fence() noexcept {
  full_fence();
}

}  // namespace ba::detail::platform
