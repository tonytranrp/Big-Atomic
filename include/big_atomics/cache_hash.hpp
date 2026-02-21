#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>

#include "big_atomics/big_atomic_me.hpp"

namespace ba {

template <typename K, typename V>
class CacheHash {
 private:
  static constexpr std::uint8_t kStateEmpty = 0;
  static constexpr std::uint8_t kStateOccupied = 1;
  static constexpr std::uint8_t kStateTombstone = 2;

  static constexpr std::size_t kRawEntryBytes = sizeof(K) + sizeof(V) + sizeof(std::uint8_t);
  static constexpr std::size_t kRoundedEntryBytes =
    ((kRawEntryBytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t)) * sizeof(std::uint64_t);
  static constexpr std::size_t kPadBytes = kRoundedEntryBytes - kRawEntryBytes;

  struct alignas(8) Entry {
    std::array<std::uint8_t, sizeof(K)> key_bytes{};
    std::array<std::uint8_t, sizeof(V)> value_bytes{};
    std::uint8_t state{kStateEmpty};
    std::array<std::uint8_t, kPadBytes> padding{};
  };

 public:
  static_assert(std::is_trivially_copyable_v<K>, "[CacheHash] K must be trivially copyable.");
  static_assert(std::is_trivially_destructible_v<K>, "[CacheHash] K must be trivially destructible.");
  static_assert(std::is_standard_layout_v<K>, "[CacheHash] K must be standard layout.");
  static_assert(
    std::has_unique_object_representations_v<K>,
    "[CacheHash] K must have unique object representations.");

  static_assert(std::is_trivially_copyable_v<V>, "[CacheHash] V must be trivially copyable.");
  static_assert(std::is_trivially_destructible_v<V>, "[CacheHash] V must be trivially destructible.");
  static_assert(std::is_standard_layout_v<V>, "[CacheHash] V must be standard layout.");
  static_assert(
    std::has_unique_object_representations_v<V>,
    "[CacheHash] V must have unique object representations.");

  static_assert(
    std::is_default_constructible_v<std::hash<K>>,
    "[CacheHash] std::hash<K> must be available for K.");
  static_assert(detail::BigAtomicSafe<Entry>, "[CacheHash] Entry must satisfy BigAtomicSafe<Entry>.");

  explicit CacheHash(std::size_t requested_capacity = 1024)
      : capacity_(sanitize_capacity(requested_capacity)),
        mask_(capacity_ - 1),
        buckets_(std::make_unique<BigAtomic<Entry>[]>(capacity_)) {
    const Entry empty = make_empty_entry();
    for (std::size_t i = 0; i < capacity_; ++i) {
      buckets_[i].store(empty);
    }
  }

  bool insert(const K& key, const V& value) noexcept {
    for (;;) {
      std::size_t candidate = capacity_;
      const std::size_t start = index_for_key(key);

      for (std::size_t probe = 0; probe < capacity_; ++probe) {
        const std::size_t idx = (start + probe) & mask_;
        const Entry current = buckets_[idx].load();

        if (current.state == kStateOccupied && key_equals(current, key)) {
          return false;
        }

        if (candidate == capacity_ && current.state != kStateOccupied) {
          candidate = idx;
        }

        if (current.state == kStateEmpty) {
          break;
        }
      }

      if (candidate == capacity_) {
        return false;
      }

      Entry expected = buckets_[candidate].load();
      if (expected.state == kStateOccupied) {
        continue;
      }

      const Entry desired = make_occupied_entry(key, value);
      if (buckets_[candidate].compare_exchange_strong(expected, desired)) {
        size_.fetch_add(1, std::memory_order_seq_cst);
        return true;
      }

      if (expected.state == kStateOccupied && key_equals(expected, key)) {
        return false;
      }
    }
  }

  bool find(const K& key, V& value_out) const noexcept {
    const std::size_t start = index_for_key(key);
    for (std::size_t probe = 0; probe < capacity_; ++probe) {
      const std::size_t idx = (start + probe) & mask_;
      const Entry current = buckets_[idx].load();
      if (current.state == kStateEmpty) {
        return false;
      }
      if (current.state == kStateOccupied && key_equals(current, key)) {
        value_out = decode_value(current);
        return true;
      }
    }
    return false;
  }

  bool erase(const K& key) noexcept {
    for (;;) {
      const std::size_t start = index_for_key(key);
      bool restart = false;

      for (std::size_t probe = 0; probe < capacity_; ++probe) {
        const std::size_t idx = (start + probe) & mask_;
        Entry current = buckets_[idx].load();

        if (current.state == kStateEmpty) {
          return false;
        }

        if (current.state == kStateOccupied && key_equals(current, key)) {
          Entry desired = current;
          desired.state = kStateTombstone;
          if (buckets_[idx].compare_exchange_strong(current, desired)) {
            size_.fetch_sub(1, std::memory_order_seq_cst);
            return true;
          }
          restart = true;
          break;
        }
      }

      if (!restart) {
        return false;
      }
    }
  }

  std::size_t size() const noexcept {
    return size_.load(std::memory_order_seq_cst);
  }

  std::size_t capacity() const noexcept {
    return capacity_;
  }

 private:
  static std::size_t sanitize_capacity(std::size_t requested) noexcept {
    const std::size_t minimum = 8;
    if (requested < minimum) {
      requested = minimum;
    }
    return std::bit_ceil(requested);
  }

  std::size_t index_for_key(const K& key) const noexcept {
    const std::size_t hash_value = std::hash<K>{}(key);
    return hash_value & mask_;
  }

  static Entry make_empty_entry() noexcept {
    Entry entry{};
    entry.state = kStateEmpty;
    return entry;
  }

  static Entry make_occupied_entry(const K& key, const V& value) noexcept {
    Entry entry{};
    std::memcpy(entry.key_bytes.data(), &key, sizeof(K));
    std::memcpy(entry.value_bytes.data(), &value, sizeof(V));
    entry.state = kStateOccupied;
    return entry;
  }

  static bool key_equals(const Entry& entry, const K& key) noexcept {
    std::array<std::uint8_t, sizeof(K)> key_bytes{};
    std::memcpy(key_bytes.data(), &key, sizeof(K));
    return std::memcmp(entry.key_bytes.data(), key_bytes.data(), sizeof(K)) == 0;
  }

  static V decode_value(const Entry& entry) noexcept {
    V value{};
    std::memcpy(&value, entry.value_bytes.data(), sizeof(V));
    return value;
  }

  const std::size_t capacity_;
  const std::size_t mask_;
  std::unique_ptr<BigAtomic<Entry>[]> buckets_;
  std::atomic<std::size_t> size_{0};
};

}  // namespace ba
