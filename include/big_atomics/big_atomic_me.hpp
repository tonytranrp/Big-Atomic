#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
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

  BigAtomic() noexcept
      : version_(0),
        backup_(detail::tagged_null<detail::Node<T>>(0)),
        cache_{} {
    detail::bytewise_atomic_store(cache_, T{});
  }

  explicit BigAtomic(const T& initial) noexcept
      : version_(0),
        backup_(detail::tagged_null<detail::Node<T>>(0)),
        cache_{} {
    detail::bytewise_atomic_store(cache_, initial);
  }

  BigAtomic(const BigAtomic&) = delete;
  BigAtomic& operator=(const BigAtomic&) = delete;

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
    for (;;) {
      const std::uint64_t version_snapshot = version_.load(std::memory_order_seq_cst);
      const T cached = detail::bytewise_atomic_load(cache_);
      detail::platform::full_fence();
      detail::Node<T>* backup_ptr = backup_.load(std::memory_order_seq_cst);

      if (detail::is_tagged_null(backup_ptr) &&
          ((version_snapshot & 1u) == 0u) &&
          version_snapshot == version_.load(std::memory_order_seq_cst)) {
        return cached;
      }

      if (!detail::is_real_node_ptr(backup_ptr)) {
        continue;
      }

      auto& pool = detail::thread_pool<T>();
      if (!pool.protect(backup_ptr)) {
        continue;
      }

      T result = backup_ptr->value;
      pool.unprotect(backup_ptr);
      return result;
    }
  }

  bool impl_cas(const T& expected, const T& desired) noexcept {
    const std::uint64_t initial_version = version_.load(std::memory_order_seq_cst);
    const T observed = impl_load();
    if (!detail::bytewise_equal(observed, expected)) {
      return false;
    }

    if (detail::bytewise_equal(expected, desired)) {
      return true;
    }

    auto& pool = detail::thread_pool<T>();
    detail::Node<T>* new_node = pool.acquire(desired);

    detail::Node<T>* expected_backup = backup_.load(std::memory_order_seq_cst);
    const detail::Node<T>* original_backup = expected_backup;
    if (backup_.compare_exchange_strong(
            expected_backup,
            new_node,
            std::memory_order_seq_cst,
            std::memory_order_seq_cst)) {
      if (detail::is_real_node_ptr(const_cast<detail::Node<T>*>(original_backup))) {
        const_cast<detail::Node<T>*>(original_backup)
            ->is_installed.store(false, std::memory_order_seq_cst);
      }
      impl_try_seqlock(initial_version, desired, new_node);
      return true;
    }

    const bool aba_window_seen =
      detail::is_real_node_ptr(const_cast<detail::Node<T>*>(original_backup)) &&
      detail::is_tagged_null(expected_backup);

    if (aba_window_seen) {
      const std::uint64_t retry_version = version_.load(std::memory_order_seq_cst);
      const T retry_cached = detail::bytewise_atomic_load(cache_);
      const bool retry_fast_path_ok =
        ((retry_version % 2u) == 0u) &&
        (retry_version == version_.load(std::memory_order_seq_cst)) &&
        detail::bytewise_equal(retry_cached, expected);

      if (retry_fast_path_ok) {
        detail::Node<T>* retry_expected = expected_backup;
        if (backup_.compare_exchange_strong(
                retry_expected,
                new_node,
                std::memory_order_seq_cst,
                std::memory_order_seq_cst)) {
          impl_try_seqlock(retry_version, desired, new_node);
          return true;
        }
      }
    }

    pool.release(new_node);
    return false;
  }

  void impl_try_seqlock(std::uint64_t version_guess, T desired, detail::Node<T>* installed) noexcept {
    for (std::size_t attempts = 0; attempts < 128; ++attempts) {
      if ((version_guess & 1u) != 0u) {
        version_guess = version_.load(std::memory_order_seq_cst);
        continue;
      }

      std::uint64_t expected_version = version_guess;
      if (!version_.compare_exchange_strong(
              expected_version,
              version_guess + 1,
              std::memory_order_seq_cst,
              std::memory_order_seq_cst)) {
        version_guess = expected_version;
        continue;
      }

      detail::bytewise_atomic_store(cache_, desired);
      const std::uint64_t published_version = version_guess + 2;
      version_.store(published_version, std::memory_order_seq_cst);

      detail::Node<T>* expected_ptr = installed;
      detail::Node<T>* tagged = detail::tagged_null<detail::Node<T>>(published_version);
      if (backup_.compare_exchange_strong(
              expected_ptr,
              tagged,
              std::memory_order_seq_cst,
              std::memory_order_seq_cst)) {
        installed->is_installed.store(false, std::memory_order_seq_cst);
        return;
      }

      if (!detail::is_real_node_ptr(expected_ptr)) {
        installed->is_installed.store(false, std::memory_order_seq_cst);
        return;
      }

      installed = expected_ptr;
      desired = installed->value;
      version_guess = version_.load(std::memory_order_seq_cst);
    }
  }

  alignas(64) std::atomic<std::uint64_t> version_;
  std::atomic<detail::Node<T>*> backup_;
  alignas(64) T cache_;
};

}  // namespace ba
