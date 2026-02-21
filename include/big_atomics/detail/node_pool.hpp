#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

#include "big_atomics/detail/platform.hpp"

namespace ba::detail {

template <typename T>
struct alignas(64) Node {
  T value{};
  std::atomic<bool> is_installed{false};
  std::atomic<bool> is_protected{false};
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
    node->is_protected.store(false, std::memory_order_seq_cst);
    node->is_installed.store(true, std::memory_order_seq_cst);
    return node;
  }

  void release(Node<T>* node) noexcept {
    if (node == nullptr) {
      return;
    }
    node->is_installed.store(false, std::memory_order_seq_cst);
    if (!node->is_protected.load(std::memory_order_seq_cst) && !node->in_free_list) {
      push_free(node);
    }
  }

  bool protect(Node<T>* node) noexcept {
    if (node == nullptr) {
      return false;
    }

    node->is_protected.store(true, std::memory_order_seq_cst);
    platform::protect_fence();

    if (!node->is_installed.load(std::memory_order_seq_cst)) {
      node->is_protected.store(false, std::memory_order_seq_cst);
      return false;
    }

    return true;
  }

  void unprotect(Node<T>* node) noexcept {
    if (node != nullptr) {
      node->is_protected.store(false, std::memory_order_seq_cst);
    }
  }

  void reclaim() noexcept {
    for (auto& node : nodes_) {
      const bool installed = node.is_installed.load(std::memory_order_seq_cst);
      const bool protected_by_reader = node.is_protected.load(std::memory_order_seq_cst);
      if (!installed && !protected_by_reader && !node.in_free_list) {
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
  thread_local NodePool<T, 3> pool;
  return pool;
}

}  // namespace ba::detail
