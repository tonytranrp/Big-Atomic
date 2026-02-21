#pragma once

#include <cstdint>
#include <type_traits>

namespace ba::detail {

template <typename T>
concept BigAtomicSafe =
  std::is_trivially_copyable_v<T> &&
  std::is_trivially_destructible_v<T> &&
  std::is_standard_layout_v<T> &&
  std::has_unique_object_representations_v<T> &&
  (sizeof(T) % sizeof(std::uint64_t) == 0) &&
  (alignof(T) >= alignof(std::uint64_t));

}  // namespace ba::detail
