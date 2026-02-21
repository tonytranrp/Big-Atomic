#include <cstdint>
#include <latch>
#include <atomic>
#include <thread>
#include <vector>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "big_atomics/cache_hash.hpp"

namespace {

using Hash = ba::CacheHash<std::uint64_t, std::uint64_t>;

}  // namespace

TEST_CASE("CacheHash single-thread insert/find/erase", "[cache_hash][single]") {
  Hash map(128);

  REQUIRE(map.insert(7, 70));
  REQUIRE(map.insert(8, 80));
  REQUIRE(map.size() == 2);

  std::uint64_t value = 0;
  REQUIRE(map.find(7, value));
  REQUIRE(value == 70);

  REQUIRE(map.erase(7));
  REQUIRE_FALSE(map.find(7, value));
  REQUIRE(map.size() == 1);
}

TEST_CASE("CacheHash duplicate insert returns false", "[cache_hash][single]") {
  Hash map(64);
  REQUIRE(map.insert(11, 111));
  REQUIRE_FALSE(map.insert(11, 222));

  std::uint64_t value = 0;
  REQUIRE(map.find(11, value));
  REQUIRE(value == 111);
}

TEST_CASE("CacheHash concurrent unique inserts", "[cache_hash][concurrency]") {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 2000;
  Hash map(32768);

  std::latch ready(kThreads);
  std::atomic<bool> insert_failed{false};
  std::atomic<bool> find_failed{false};
  std::uint64_t first_bad_key = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t first_bad_value = 0;
  std::uint64_t first_expected_value = 0;
  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([t, &map, &ready, &insert_failed]() {
      ready.arrive_and_wait();
      const std::uint64_t base = static_cast<std::uint64_t>(t) * 100000ull;
      for (int i = 0; i < kPerThread; ++i) {
        const std::uint64_t key = base + static_cast<std::uint64_t>(i);
        const std::uint64_t value = key ^ 0xBADC0FFEEull;
        if (!map.insert(key, value)) {
          insert_failed.store(true, std::memory_order_release);
          return;
        }
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  REQUIRE_FALSE(insert_failed.load(std::memory_order_acquire));
  REQUIRE(map.size() == static_cast<std::size_t>(kThreads * kPerThread));

  for (int t = 0; t < kThreads; ++t) {
    const std::uint64_t base = static_cast<std::uint64_t>(t) * 100000ull;
    for (int i = 0; i < kPerThread; ++i) {
      const std::uint64_t key = base + static_cast<std::uint64_t>(i);
      const std::uint64_t expected_value = key ^ 0xBADC0FFEEull;
      std::uint64_t value = 0;
      if (!map.find(key, value) || value != expected_value) {
        if (!find_failed.exchange(true, std::memory_order_acq_rel)) {
          first_bad_key = key;
          first_bad_value = value;
          first_expected_value = expected_value;
        }
      }
    }
  }

  INFO("first_bad_key=" << first_bad_key
       << " first_bad_value=" << first_bad_value
       << " expected_value=" << first_expected_value);
  REQUIRE_FALSE(find_failed.load(std::memory_order_acquire));
}

TEST_CASE("CacheHash concurrent erase semantics", "[cache_hash][concurrency]") {
  constexpr int kKeys = 4000;
  Hash map(8192);

  for (int i = 0; i < kKeys; ++i) {
    REQUIRE(map.insert(static_cast<std::uint64_t>(i), static_cast<std::uint64_t>(i * 3)));
  }

  std::thread eraser([&map]() {
    for (std::uint64_t i = 0; i < 2000; ++i) {
      (void)map.erase(i);
    }
  });

  std::thread finder([&map]() {
    for (std::uint64_t i = 0; i < 2000; ++i) {
      std::uint64_t value = 0;
      (void)map.find(i, value);
    }
  });

  eraser.join();
  finder.join();

  REQUIRE(map.size() <= static_cast<std::size_t>(kKeys));
}
