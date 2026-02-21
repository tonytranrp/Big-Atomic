#pragma once

#include <atomic>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "big_atomics/detail/constraints.hpp"
#include "big_atomics/detail/platform.hpp"

namespace ba::detail {

template <BigAtomicSafe T>
inline T bytewise_atomic_load_words(std::uint64_t* src_words) noexcept {
  platform::before_bytewise_load();

  constexpr std::size_t kWordCount = sizeof(T) / sizeof(std::uint64_t);
  std::array<std::uint64_t, kWordCount> words{};

  for (std::size_t i = 0; i < kWordCount; ++i) {
    std::atomic_ref<std::uint64_t> src_ref(src_words[i]);
    words[i] = src_ref.load(std::memory_order_acquire);
  }

  platform::after_bytewise_load();
  return std::bit_cast<T>(words);
}

template <BigAtomicSafe T>
inline void bytewise_atomic_store_words(std::uint64_t* dst_words, const T& src) noexcept {
  platform::before_bytewise_store();

  constexpr std::size_t kWordCount = sizeof(T) / sizeof(std::uint64_t);
  const auto words = std::bit_cast<std::array<std::uint64_t, kWordCount>>(src);

  for (std::size_t i = 0; i < kWordCount; ++i) {
    std::atomic_ref<std::uint64_t> dst_ref(dst_words[i]);
    dst_ref.store(words[i], std::memory_order_release);
  }

  platform::after_bytewise_store();
}

template <BigAtomicSafe T>
inline bool bytewise_equal(const T& lhs, const T& rhs) noexcept {
  constexpr std::size_t kWordCount = sizeof(T) / sizeof(std::uint64_t);
  const auto lhs_words = std::bit_cast<std::array<std::uint64_t, kWordCount>>(lhs);
  const auto rhs_words = std::bit_cast<std::array<std::uint64_t, kWordCount>>(rhs);
  return lhs_words == rhs_words;
}

}  // namespace ba::detail
