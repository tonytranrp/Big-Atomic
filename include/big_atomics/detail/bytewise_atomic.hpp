#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "big_atomics/detail/constraints.hpp"
#include "big_atomics/detail/platform.hpp"

namespace ba::detail {

template <BigAtomicSafe T>
inline T bytewise_atomic_load(const T& src) noexcept {
  platform::before_bytewise_load();

  T dst{};
  constexpr std::size_t kWordCount = sizeof(T) / sizeof(std::uint64_t);

  auto* dst_words = reinterpret_cast<std::uint64_t*>(&dst);
  auto* src_words = reinterpret_cast<const std::uint64_t*>(&src);
  for (std::size_t i = 0; i < kWordCount; ++i) {
    auto& mutable_src_word = const_cast<std::uint64_t&>(src_words[i]);
    std::atomic_ref<std::uint64_t> src_ref(mutable_src_word);
    dst_words[i] = src_ref.load(std::memory_order_seq_cst);
  }

  platform::after_bytewise_load();
  return dst;
}

template <BigAtomicSafe T>
inline void bytewise_atomic_store(T& dst, const T& src) noexcept {
  platform::before_bytewise_store();

  constexpr std::size_t kWordCount = sizeof(T) / sizeof(std::uint64_t);
  auto* dst_words = reinterpret_cast<std::uint64_t*>(&dst);
  auto* src_words = reinterpret_cast<const std::uint64_t*>(&src);
  for (std::size_t i = 0; i < kWordCount; ++i) {
    std::atomic_ref<std::uint64_t> dst_ref(dst_words[i]);
    dst_ref.store(src_words[i], std::memory_order_seq_cst);
  }

  platform::after_bytewise_store();
}

template <BigAtomicSafe T>
inline bool bytewise_equal(const T& lhs, const T& rhs) noexcept {
  constexpr std::size_t kWordCount = sizeof(T) / sizeof(std::uint64_t);
  auto* lhs_words = reinterpret_cast<const std::uint64_t*>(&lhs);
  auto* rhs_words = reinterpret_cast<const std::uint64_t*>(&rhs);
  for (std::size_t i = 0; i < kWordCount; ++i) {
    if (lhs_words[i] != rhs_words[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace ba::detail
