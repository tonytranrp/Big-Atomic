#pragma once

#include <cassert>
#include <cstdint>

namespace ba::detail {

inline constexpr std::uint64_t kTaggedNullVersionMask = 0xFFFFFFFFull;

template <typename NodeT>
inline NodeT* tagged_null(std::uint64_t version) noexcept {
  // Mask the version to keep the sentinel pointer canonical-safe on x64.
  const auto encoded =
    ((version & kTaggedNullVersionMask) << 1) | static_cast<std::uint64_t>(1);
  return reinterpret_cast<NodeT*>(static_cast<std::uintptr_t>(encoded));
}

template <typename NodeT>
inline bool is_tagged_null(NodeT* ptr) noexcept {
  const auto bits = reinterpret_cast<std::uintptr_t>(ptr);
  return (bits & static_cast<std::uintptr_t>(0x3F)) != 0;
}

template <typename NodeT>
inline bool is_real_node_ptr(NodeT* ptr) noexcept {
  return ptr != nullptr && !is_tagged_null(ptr);
}

template <typename NodeT>
inline std::uint32_t tagged_version(NodeT* ptr) noexcept {
  const auto bits = reinterpret_cast<std::uintptr_t>(ptr);
  return static_cast<std::uint32_t>((bits >> 1) & kTaggedNullVersionMask);
}

template <typename NodeT>
inline void assert_tagged_null_contract(NodeT* ptr) noexcept {
#if !defined(NDEBUG)
  if (is_tagged_null(ptr)) {
    const auto bits = reinterpret_cast<std::uintptr_t>(ptr);
    assert((bits & static_cast<std::uintptr_t>(1)) == 1);
  }
#else
  (void)ptr;
#endif
}

}  // namespace ba::detail
