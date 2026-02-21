#pragma once

#include <cstdint>

namespace ba::detail {

template <typename PtrT>
inline PtrT* mark_ptr(PtrT* ptr) noexcept {
  const auto raw = reinterpret_cast<std::uintptr_t>(ptr);
  return reinterpret_cast<PtrT*>(raw | static_cast<std::uintptr_t>(1));
}

template <typename PtrT>
inline PtrT* unmark_ptr(PtrT* ptr) noexcept {
  const auto raw = reinterpret_cast<std::uintptr_t>(ptr);
  return reinterpret_cast<PtrT*>(raw & ~static_cast<std::uintptr_t>(1));
}

template <typename PtrT>
inline bool is_marked_ptr(PtrT* ptr) noexcept {
  const auto raw = reinterpret_cast<std::uintptr_t>(ptr);
  return (raw & static_cast<std::uintptr_t>(1)) != 0;
}

}  // namespace ba::detail
