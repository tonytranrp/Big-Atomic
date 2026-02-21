#include <atomic>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "big_atomics/big_atomic_me.hpp"

namespace {

struct alignas(8) ThreeWords {
  std::uint64_t a;
  std::uint64_t b;
  std::uint64_t c;
};

static_assert(ba::detail::BigAtomicSafe<ThreeWords>);

inline bool all_equal(const ThreeWords& value) {
  return value.a == value.b && value.b == value.c;
}

}  // namespace

TEST_CASE("Paper API load/store roundtrip", "[correctness][paper]") {
  ba::BigAtomic<ThreeWords> value;
  value.ba_store(ThreeWords{7, 7, 7});
  const ThreeWords loaded = value.ba_load();
  REQUIRE(loaded.a == 7);
  REQUIRE(loaded.b == 7);
  REQUIRE(loaded.c == 7);
}

TEST_CASE("Paper API CAS success and failure", "[correctness][paper]") {
  ba::BigAtomic<ThreeWords> value(ThreeWords{1, 1, 1});
  REQUIRE(value.ba_cas(ThreeWords{1, 1, 1}, ThreeWords{2, 2, 2}));
  REQUIRE_FALSE(value.ba_cas(ThreeWords{1, 1, 1}, ThreeWords{9, 9, 9}));
  const ThreeWords loaded = value.ba_load();
  REQUIRE(loaded.a == 2);
  REQUIRE(loaded.b == 2);
  REQUIRE(loaded.c == 2);
}

TEST_CASE("std compare_exchange updates expected on failure", "[correctness][std]") {
  ba::BigAtomic<ThreeWords> value(ThreeWords{3, 3, 3});
  ThreeWords expected{5, 5, 5};
  const bool exchanged = value.compare_exchange_strong(expected, ThreeWords{6, 6, 6});
  REQUIRE_FALSE(exchanged);
  REQUIRE(expected.a == 3);
  REQUIRE(expected.b == 3);
  REQUIRE(expected.c == 3);
}

TEST_CASE("std exchange returns previous value", "[correctness][std]") {
  ba::BigAtomic<ThreeWords> value(ThreeWords{10, 10, 10});
  const ThreeWords previous = value.exchange(ThreeWords{11, 11, 11});
  REQUIRE(previous.a == 10);
  REQUIRE(previous.b == 10);
  REQUIRE(previous.c == 10);
  const ThreeWords now = value.load();
  REQUIRE(now.a == 11);
  REQUIRE(now.b == 11);
  REQUIRE(now.c == 11);
}

TEST_CASE("Concurrent non-tearing reads", "[correctness][concurrency]") {
  ba::BigAtomic<ThreeWords> value(ThreeWords{0, 0, 0});
  constexpr int kThreads = 8;
  constexpr int kOps = 40000;

  std::latch ready(kThreads);
  std::atomic<bool> tearing_found{false};

  auto worker = [&](int index) {
    ready.arrive_and_wait();
    for (int i = 0; i < kOps; ++i) {
      if ((index % 2) == 0) {
        const std::uint64_t n = static_cast<std::uint64_t>(index) * 100000ull +
                                static_cast<std::uint64_t>(i);
        value.store(ThreeWords{n, n, n});
      } else {
        const ThreeWords observed = value.load();
        if (!all_equal(observed)) {
          tearing_found.store(true, std::memory_order_release);
          return;
        }
      }
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back(worker, i);
  }
  for (auto& thread : threads) {
    thread.join();
  }

  REQUIRE_FALSE(tearing_found.load(std::memory_order_acquire));
}

TEST_CASE("Reclaim stress under contention", "[correctness][reclaim]") {
  ba::BigAtomic<ThreeWords> value(ThreeWords{0, 0, 0});
  constexpr int kWriters = 4;
  constexpr int kReaders = 4;
  constexpr int kIterations = 25000;

  std::latch ready(kWriters + kReaders);
  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> read_count{0};

  auto writer = [&](int index) {
    ready.arrive_and_wait();
    for (int i = 0; i < kIterations; ++i) {
      const std::uint64_t n = static_cast<std::uint64_t>(index) * 1000000ull +
                              static_cast<std::uint64_t>(i);
      value.ba_store(ThreeWords{n, n, n});
    }
  };

  auto reader = [&]() {
    ready.arrive_and_wait();
    while (!stop.load(std::memory_order_acquire)) {
      (void)value.ba_load();
      read_count.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::vector<std::thread> writers;
  std::vector<std::thread> readers;
  writers.reserve(kWriters);
  readers.reserve(kReaders);

  for (int i = 0; i < kWriters; ++i) {
    writers.emplace_back(writer, i);
  }
  for (int i = 0; i < kReaders; ++i) {
    readers.emplace_back(reader);
  }

  for (auto& thread : writers) {
    thread.join();
  }
  stop.store(true, std::memory_order_release);
  for (auto& thread : readers) {
    thread.join();
  }

  REQUIRE(read_count.load(std::memory_order_acquire) > 0);
  const ThreeWords final_value = value.ba_load();
  REQUIRE(all_equal(final_value));
}
