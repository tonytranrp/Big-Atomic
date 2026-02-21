#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <thread>

namespace ba::detail::trace {

inline void log_event(
    const char* event_name,
    std::uint64_t value_a = 0,
    std::uint64_t value_b = 0) noexcept {
#if defined(BA_TRACE_CONCURRENCY) && BA_TRACE_CONCURRENCY
  const auto tid_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
  std::fprintf(
    stderr,
    "[BA_TRACE] event=%s tid=%zu a=%llu b=%llu\n",
    event_name,
    static_cast<std::size_t>(tid_hash),
    static_cast<unsigned long long>(value_a),
    static_cast<unsigned long long>(value_b));
#else
  (void)event_name;
  (void)value_a;
  (void)value_b;
#endif
}

}  // namespace ba::detail::trace
