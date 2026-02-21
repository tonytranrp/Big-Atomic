#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

#include "big_atomics/detail/bytewise_atomic.hpp"
#include "big_atomics/detail/constraints.hpp"
#include "big_atomics/detail/node_pool.hpp"
#include "big_atomics/detail/platform.hpp"
#include "big_atomics/detail/tagged_null.hpp"

namespace ba {

template <detail::BigAtomicSafe T>
class BigAtomic {
 public:
  using value_type = T;

  static constexpr bool is_always_lock_free = false;
  static constexpr std::size_t kWordCount = sizeof(T) / sizeof(std::uint64_t);

  BigAtomic() noexcept
      : version_(0),
        backup_(detail::tagged_null<detail::Node<T>>(0)),
        cache_words_{} {
    const T initial{};
    detail::bytewise_atomic_store_words<T>(cache_words_.data(), initial);
  }

  explicit BigAtomic(const T& initial) noexcept
      : version_(0),
        backup_(detail::tagged_null<detail::Node<T>>(0)),
        cache_words_{} {
    detail::bytewise_atomic_store_words<T>(cache_words_.data(), initial);
  }

  BigAtomic(const BigAtomic&) = delete;
  BigAtomic& operator=(const BigAtomic&) = delete;

  ~BigAtomic() noexcept {
    detail::Node<T>* backup_snapshot = backup_.load(std::memory_order_acquire);
    if (detail::is_real_node_ptr(backup_snapshot)) {
      backup_snapshot->is_installed.store(false, std::memory_order_release);
    }
  }

  // Paper API
  T ba_load() const noexcept {
    return const_cast<BigAtomic*>(this)->impl_load();
  }

  void ba_store(const T& desired) noexcept {
    while (!impl_cas(impl_load(), desired)) {
    }
  }

  bool ba_cas(const T& expected, const T& desired) noexcept {
    return impl_cas(expected, desired);
  }

  // std::atomic-style API
  T load(std::memory_order = std::memory_order_seq_cst) const noexcept {
    return ba_load();
  }

  void store(const T& desired, std::memory_order = std::memory_order_seq_cst) noexcept {
    ba_store(desired);
  }

  bool compare_exchange_strong(
      T& expected,
      const T& desired,
      std::memory_order = std::memory_order_seq_cst,
      std::memory_order = std::memory_order_seq_cst) noexcept {
    const T snapshot = expected;
    if (impl_cas(snapshot, desired)) {
      return true;
    }
    expected = impl_load();
    return false;
  }

  bool compare_exchange_weak(
      T& expected,
      const T& desired,
      std::memory_order success = std::memory_order_seq_cst,
      std::memory_order failure = std::memory_order_seq_cst) noexcept {
    return compare_exchange_strong(expected, desired, success, failure);
  }

  T exchange(const T& desired, std::memory_order = std::memory_order_seq_cst) noexcept {
    T current = impl_load();
    while (!impl_cas(current, desired)) {
      current = impl_load();
    }
    return current;
  }

  bool is_lock_free() const noexcept {
    return false;
  }

  BigAtomic& operator=(const T& value) noexcept {
    store(value);
    return *this;
  }

  operator T() const noexcept {
    return load();
  }

 private:
  static_assert(
    std::atomic<detail::Node<T>*>::is_always_lock_free,
    "[BigAtomic] Pointer-sized atomics must be lock-free on this platform."
  );

  T impl_load() noexcept {
    auto& pool = detail::thread_pool<T>();
    for (;;) {
      const std::uint64_t v0 = version_.load(std::memory_order_seq_cst);
      if ((v0 & 1u) != 0u) {
        adaptive_backoff(0);
        continue;
      }

      const T snapshot = detail::bytewise_atomic_load_words<T>(cache_words_.data());
      detail::Node<T>* backup_snapshot = backup_.load(std::memory_order_seq_cst);
      const std::uint64_t v1 = version_.load(std::memory_order_seq_cst);
      if ((v0 == v1) && ((v1 & 1u) == 0u) && detail::is_tagged_null(backup_snapshot)) {
        const T snapshot_2 = detail::bytewise_atomic_load_words<T>(cache_words_.data());
        detail::Node<T>* backup_snapshot_2 = backup_.load(std::memory_order_seq_cst);
        const std::uint64_t v2 = version_.load(std::memory_order_seq_cst);
        if ((v1 == v2) &&
            ((v2 & 1u) == 0u) &&
            detail::is_tagged_null(backup_snapshot_2) &&
            detail::bytewise_equal(snapshot, snapshot_2)) {
          return snapshot_2;
        }
        adaptive_backoff(0);
        continue;
      }

      detail::Node<T>* protected_node = detail::protect_from_atomic(backup_, pool);
      if (protected_node == nullptr) {
        adaptive_backoff(1);
        continue;
      }

      const T result = protected_node->value;
      pool.unprotect(protected_node);
      return result;
    }
  }

  bool impl_cas(const T& expected, const T& desired) noexcept {
    const T observed = impl_load();
    if (!detail::bytewise_equal(observed, expected)) {
      return false;
    }

    if (detail::bytewise_equal(expected, desired)) {
      return true;
    }

    auto& pool = detail::thread_pool<T>();
    detail::Node<T>* new_node = pool.acquire(desired);
    detail::Node<T>* expected_backup = backup_.load(std::memory_order_acquire);
    const detail::Node<T>* original_backup = expected_backup;

    if (detail::is_real_node_ptr(expected_backup)) {
      if (!pool.protect(expected_backup)) {
        pool.release(new_node);
        return false;
      }

      detail::Node<T>* stable = backup_.load(std::memory_order_acquire);
      const bool matches_observed =
        (stable == expected_backup) &&
        detail::bytewise_equal(expected_backup->value, expected);
      pool.unprotect(expected_backup);
      if (!matches_observed) {
        pool.release(new_node);
        return false;
      }
    } else {
      const std::uint64_t guard_version = version_.load(std::memory_order_seq_cst);
      const T guard_cached = detail::bytewise_atomic_load_words<T>(cache_words_.data());
      const bool guard_ok =
        ((guard_version % 2u) == 0u) &&
        (guard_version == version_.load(std::memory_order_seq_cst)) &&
        detail::bytewise_equal(guard_cached, expected);
      if (!guard_ok) {
        pool.release(new_node);
        return false;
      }
    }

    if (backup_.compare_exchange_strong(
            expected_backup,
            new_node,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      if (detail::is_real_node_ptr(const_cast<detail::Node<T>*>(original_backup))) {
        const_cast<detail::Node<T>*>(original_backup)
            ->is_installed.store(false, std::memory_order_release);
      }

      const std::uint64_t initial_version = version_.load(std::memory_order_seq_cst);
      impl_try_seqlock(initial_version, desired, new_node);
      return true;
    }

    const bool aba_window_seen =
      detail::is_real_node_ptr(const_cast<detail::Node<T>*>(original_backup)) &&
      detail::is_tagged_null(expected_backup);
    if (aba_window_seen) {
      const std::uint64_t retry_version = version_.load(std::memory_order_seq_cst);
      const T retry_cached = detail::bytewise_atomic_load_words<T>(cache_words_.data());
      const bool retry_fast_path_ok =
        ((retry_version % 2u) == 0u) &&
        (retry_version == version_.load(std::memory_order_seq_cst)) &&
        detail::bytewise_equal(retry_cached, expected);

      if (retry_fast_path_ok) {
        detail::Node<T>* retry_expected = expected_backup;
        if (backup_.compare_exchange_strong(
                retry_expected,
                new_node,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
          impl_try_seqlock(retry_version, desired, new_node);
          return true;
        }
      }
    }

    pool.release(new_node);
    return false;
  }

  void impl_try_seqlock(std::uint64_t version_guess, T desired, detail::Node<T>* installed) noexcept {
    std::size_t attempts = 0;
    for (;;) {
      if ((version_guess & 1u) != 0u) {
        version_guess = version_.load(std::memory_order_seq_cst);
        adaptive_backoff(attempts);
        ++attempts;
        continue;
      }

      std::uint64_t expected_version = version_guess;
      if (!version_.compare_exchange_strong(
              expected_version,
              version_guess + 1,
              std::memory_order_seq_cst,
              std::memory_order_seq_cst)) {
        version_guess = expected_version;
        adaptive_backoff(attempts);
        ++attempts;
        continue;
      }

      detail::bytewise_atomic_store_words<T>(cache_words_.data(), desired);
      const std::uint64_t published_version = version_guess + 2;
      version_.store(published_version, std::memory_order_seq_cst);

      detail::Node<T>* expected_ptr = installed;
      detail::Node<T>* tagged = detail::tagged_null<detail::Node<T>>(published_version);
      if (backup_.compare_exchange_strong(
              expected_ptr,
              tagged,
              std::memory_order_acq_rel,
              std::memory_order_acquire)) {
        installed->is_installed.store(false, std::memory_order_release);
        return;
      }

      // Another writer replaced backup_ while we published cache.
      // Do not read or help using foreign nodes without explicit protection.
      // Keep cache conservative: backup_ remains non-tagged and readers take slow path.
      installed->is_installed.store(false, std::memory_order_release);
      return;
    }
  }

  static void adaptive_backoff(std::size_t attempts) noexcept {
    if (attempts < 8) {
      const std::size_t spins = static_cast<std::size_t>(1) << attempts;
      for (std::size_t i = 0; i < spins; ++i) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
      }
      return;
    }
    std::this_thread::yield();
  }

  alignas(64) std::atomic<std::uint64_t> version_;
  std::atomic<detail::Node<T>*> backup_;
  alignas(64) std::array<std::uint64_t, kWordCount> cache_words_;
};

}  // namespace ba
