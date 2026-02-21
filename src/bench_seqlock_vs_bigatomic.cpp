#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

#include <benchmark/benchmark.h>

#include "big_atomics/big_atomic_me.hpp"

namespace {

struct alignas(8) ThreeWords {
  std::uint64_t a;
  std::uint64_t b;
  std::uint64_t c;
};

static_assert(ba::detail::BigAtomicSafe<ThreeWords>);

template <typename T>
class SeqLock {
 public:
  SeqLock() = default;
  explicit SeqLock(const T& initial) : value_(initial) {}

  T load() const noexcept {
    for (;;) {
      const std::uint64_t begin = sequence_.load(std::memory_order_acquire);
      if ((begin & 1u) != 0u) {
        continue;
      }

      T copy{};
      std::memcpy(&copy, &value_, sizeof(T));

      const std::uint64_t end = sequence_.load(std::memory_order_acquire);
      if (begin == end) {
        return copy;
      }
    }
  }

  void store(const T& desired) noexcept {
    sequence_.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(&value_, &desired, sizeof(T));
    std::atomic_thread_fence(std::memory_order_release);
    sequence_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  mutable std::atomic<std::uint64_t> sequence_{0};
  alignas(64) T value_{};
};

ba::BigAtomic<ThreeWords> g_big_atomic(ThreeWords{1, 1, 1});
SeqLock<ThreeWords> g_seq_lock(ThreeWords{1, 1, 1});

static void BM_BigAtomic_Mixed(benchmark::State& state) {
  for (auto _ : state) {
    if ((state.thread_index() & 1) == 0) {
      ThreeWords value = g_big_atomic.load();
      benchmark::DoNotOptimize(value);
    } else {
      const std::uint64_t n = static_cast<std::uint64_t>(state.iterations());
      g_big_atomic.store(ThreeWords{n, n, n});
    }
  }
  state.SetItemsProcessed(state.iterations());
}

static void BM_SeqLock_Mixed(benchmark::State& state) {
  for (auto _ : state) {
    if ((state.thread_index() & 1) == 0) {
      ThreeWords value = g_seq_lock.load();
      benchmark::DoNotOptimize(value);
    } else {
      const std::uint64_t n = static_cast<std::uint64_t>(state.iterations());
      g_seq_lock.store(ThreeWords{n, n, n});
    }
  }
  state.SetItemsProcessed(state.iterations());
}

void ApplyThreadRange(benchmark::internal::Benchmark* bench) {
  const int hw = static_cast<int>(std::thread::hardware_concurrency());
  const int max_threads = hw > 0 ? hw * 2 : 16;
  bench->ThreadRange(1, max_threads)->UseRealTime();
}

}  // namespace

BENCHMARK(BM_BigAtomic_Mixed)->Apply(ApplyThreadRange);
BENCHMARK(BM_SeqLock_Mixed)->Apply(ApplyThreadRange);

BENCHMARK_MAIN();
