#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <benchmark/benchmark.h>

#include "big_atomics/cache_hash.hpp"

namespace {

class MutexMap {
 public:
  explicit MutexMap(std::size_t reserve_count) {
    map_.reserve(reserve_count);
  }

  bool insert(std::uint64_t key, std::uint64_t value) {
    std::lock_guard<std::mutex> lock(mu_);
    return map_.emplace(key, value).second;
  }

  bool find(std::uint64_t key, std::uint64_t& value) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = map_.find(key);
    if (it == map_.end()) {
      return false;
    }
    value = it->second;
    return true;
  }

 private:
  std::mutex mu_;
  std::unordered_map<std::uint64_t, std::uint64_t> map_{};
};

ba::CacheHash<std::uint64_t, std::uint64_t> g_cache_hash(1 << 16);
MutexMap g_mutex_map(1 << 16);
std::once_flag g_seed_once;

void seed_maps_once() {
  std::call_once(g_seed_once, []() {
    for (std::uint64_t i = 0; i < 50000; ++i) {
      const std::uint64_t value = i ^ 0xABCDEFu;
      (void)g_cache_hash.insert(i, value);
      (void)g_mutex_map.insert(i, value);
    }
  });
}

void ApplyThreadRange(benchmark::internal::Benchmark* bench) {
  const int hw = static_cast<int>(std::thread::hardware_concurrency());
  const int max_threads = hw > 0 ? hw * 2 : 16;
  bench->ThreadRange(1, max_threads)->UseRealTime();
}

static void BM_CacheHash_ReadHeavy(benchmark::State& state) {
  seed_maps_once();
  std::uint64_t out = 0;
  for (auto _ : state) {
    const std::uint64_t key = static_cast<std::uint64_t>(state.iterations() % 50000);
    benchmark::DoNotOptimize(g_cache_hash.find(key, out));
    benchmark::DoNotOptimize(out);
  }
  state.SetItemsProcessed(state.iterations());
}

static void BM_MutexMap_ReadHeavy(benchmark::State& state) {
  seed_maps_once();
  std::uint64_t out = 0;
  for (auto _ : state) {
    const std::uint64_t key = static_cast<std::uint64_t>(state.iterations() % 50000);
    benchmark::DoNotOptimize(g_mutex_map.find(key, out));
    benchmark::DoNotOptimize(out);
  }
  state.SetItemsProcessed(state.iterations());
}

static void BM_CacheHash_Mixed(benchmark::State& state) {
  seed_maps_once();
  std::uint64_t out = 0;
  for (auto _ : state) {
    const std::uint64_t key = static_cast<std::uint64_t>((state.iterations() + state.thread_index()) % 50000);
    if ((state.thread_index() & 1) == 0) {
      benchmark::DoNotOptimize(g_cache_hash.find(key, out));
    } else {
      (void)g_cache_hash.insert(1000000ull + key, key ^ 0x11111111u);
    }
  }
  state.SetItemsProcessed(state.iterations());
}

static void BM_MutexMap_Mixed(benchmark::State& state) {
  seed_maps_once();
  std::uint64_t out = 0;
  for (auto _ : state) {
    const std::uint64_t key = static_cast<std::uint64_t>((state.iterations() + state.thread_index()) % 50000);
    if ((state.thread_index() & 1) == 0) {
      benchmark::DoNotOptimize(g_mutex_map.find(key, out));
    } else {
      (void)g_mutex_map.insert(1000000ull + key, key ^ 0x11111111u);
    }
  }
  state.SetItemsProcessed(state.iterations());
}

}  // namespace

BENCHMARK(BM_CacheHash_ReadHeavy)->Apply(ApplyThreadRange);
BENCHMARK(BM_MutexMap_ReadHeavy)->Apply(ApplyThreadRange);
BENCHMARK(BM_CacheHash_Mixed)->Apply(ApplyThreadRange);
BENCHMARK(BM_MutexMap_Mixed)->Apply(ApplyThreadRange);

BENCHMARK_MAIN();
