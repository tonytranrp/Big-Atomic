#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "big_atomics/detail/platform.hpp"
#include "big_atomics/detail/tagged_null.hpp"

namespace ba::detail {

template <typename T>
struct alignas(64) Node {
  T value{};
  std::atomic<bool> is_installed{false};
  std::atomic<std::uint32_t> protected_count{0};
  Node* next_free{nullptr};
  bool in_free_list{false};
};

template <typename T, std::size_t PoolSize = 3>
class NodePool {
 public:
  NodePool() noexcept {
    for (auto& node : nodes_) {
      push_free(&node);
    }
  }

  Node<T>* acquire(const T& value) noexcept {
    if (free_head_ == nullptr) {
      reclaim();
    }

    while (free_head_ == nullptr) {
      std::this_thread::yield();
      reclaim();
    }

    Node<T>* node = pop_free();
    node->value = value;
    node->protected_count.store(0, std::memory_order_release);
    node->is_installed.store(true, std::memory_order_release);
    return node;
  }

  void release(Node<T>* node) noexcept {
    if (node == nullptr) {
      return;
    }
    node->is_installed.store(false, std::memory_order_release);
    if (node->protected_count.load(std::memory_order_acquire) == 0 && !node->in_free_list) {
      push_free(node);
    }
  }

  bool protect(Node<T>* node) noexcept {
    if (node == nullptr) {
      return false;
    }

    node->protected_count.fetch_add(1, std::memory_order_seq_cst);
    platform::protect_fence();

    if (!node->is_installed.load(std::memory_order_acquire)) {
      node->protected_count.fetch_sub(1, std::memory_order_seq_cst);
      return false;
    }

    return true;
  }

  void unprotect(Node<T>* node) noexcept {
    if (node != nullptr) {
      node->protected_count.fetch_sub(1, std::memory_order_seq_cst);
    }
  }

  void reclaim() noexcept {
    for (auto& node : nodes_) {
      const bool installed = node.is_installed.load(std::memory_order_acquire);
      const auto protected_by_readers = node.protected_count.load(std::memory_order_acquire);
      if (!installed && protected_by_readers == 0 && !node.in_free_list) {
        push_free(&node);
      }
    }
  }

 private:
  void push_free(Node<T>* node) noexcept {
    node->next_free = free_head_;
    node->in_free_list = true;
    free_head_ = node;
  }

  Node<T>* pop_free() noexcept {
    Node<T>* node = free_head_;
    free_head_ = free_head_->next_free;
    node->next_free = nullptr;
    node->in_free_list = false;
    return node;
  }

  std::array<Node<T>, PoolSize> nodes_{};
  Node<T>* free_head_{nullptr};
};

template <typename T>
inline NodePool<T, 3>& thread_pool() noexcept {
  // BigAtomic may publish pointers to per-thread nodes into shared state.
  // Keep pool storage process-lifetime to avoid thread-exit dangling pointers.
  thread_local NodePool<T, 3>* pool = new NodePool<T, 3>();
  return *pool;
}

template <typename T, std::size_t PoolSize = 3>
inline Node<T>* protect_from_atomic(
    std::atomic<Node<T>*>& source,
    NodePool<T, PoolSize>& pool) noexcept {
  for (;;) {
    Node<T>* candidate = source.load(std::memory_order_acquire);
    if (!is_real_node_ptr(candidate)) {
      return nullptr;
    }

    if (!pool.protect(candidate)) {
      continue;
    }

    Node<T>* stable = source.load(std::memory_order_acquire);
    if (stable == candidate) {
      return candidate;
    }

    pool.unprotect(candidate);

    if (!is_real_node_ptr(stable)) {
      return nullptr;
    }
  }
}

}  // namespace ba::detail
