// SPDX-License-Identifier: MIT
//
// Lock-free multi-producer single-consumer queue (MPSC), C++20
//
// 设计目标：
// - 多生产者线程并发 push
// - 单消费者线程 pop
// - 无锁、无阻塞（仅使用原子操作）
// - 有界队列（固定容量环形缓冲区）
//
// 算法：基于环形缓冲区的 MPSC 队列
// - head: 消费者读取位置（单线程，无需原子）
// - tail: 生产者写入位置（多线程，需要原子）
// - 使用 CAS 操作实现多生产者竞争

#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>

namespace usn {

template <class T>
class MpscQueue {
    static_assert(!std::is_reference_v<T>, "T must not be a reference");
    static_assert(std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>,
                  "T must be move or copy constructible");

public:
    using value_type = T;
    using size_type = std::size_t;

    explicit MpscQueue(size_type capacity)
        : capacity_(round_up_power_of_two(capacity)),
          mask_(capacity_ - 1),
          buffer_(capacity_) {
        static_assert(
            std::atomic<size_type>::is_always_lock_free,
            "size_type atomics must be lock-free on this platform"
        );
        
        // 初始化原子变量
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        
        // 初始化 buffer（使用 optional 标记是否已写入）
        for (auto& slot : buffer_) {
            slot = std::nullopt;
        }
    }

    MpscQueue(const MpscQueue&) = delete;
    MpscQueue& operator=(const MpscQueue&) = delete;

    // 多生产者 push；成功返回 true，队列满返回 false
    template <class U>
    bool try_push(U&& value) {
        static_assert(std::is_same_v<std::decay_t<U>, T>,
                      "value type must match T");
        
        size_type tail = tail_.load(std::memory_order_relaxed);
        
        for (;;) {
            // 读取 head（acquire 语义：确保看到最新的 head）
            size_type head = head_.load(std::memory_order_acquire);
            
            // 检查队列是否满
            if (tail - head >= capacity_) {
                return false;  // 队列满
            }
            
            // 尝试抢占一个 slot（CAS）
            if (tail_.compare_exchange_weak(
                    tail, tail + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                // CAS 成功，获得写入位置
                break;
            }
            // CAS 失败，tail 已被其他线程更新，重新尝试
        }
        
        // 写入数据到 buffer
        const size_type index = tail & mask_;
        buffer_[index] = std::forward<U>(value);
        
        return true;
    }

    // 单消费者 pop；成功返回 value，队列空返回 nullopt
    std::optional<T> try_pop() {
        size_type head = head_.load(std::memory_order_relaxed);
        size_type tail = tail_.load(std::memory_order_acquire);
        
        if (head >= tail) {
            return std::nullopt;  // 队列空
        }
        
        const size_type index = head & mask_;
        auto& slot = buffer_[index];
        
        if (!slot.has_value()) {
            // 生产者还未写入完成，等待（spin）
            // 注意：在实际应用中，可能需要更复杂的等待策略
            return std::nullopt;
        }
        
        // 读取值
        T value = std::move(*slot);
        slot = std::nullopt;
        
        // 更新 head（relaxed 即可，因为只有单消费者）
        head_.store(head + 1, std::memory_order_release);
        
        return value;
    }

    // 检查队列是否为空（近似值，因为多线程环境）
    bool empty() const noexcept {
        size_type head = head_.load(std::memory_order_acquire);
        size_type tail = tail_.load(std::memory_order_acquire);
        return head >= tail;
    }

    // 获取队列大小（近似值）
    size_type size() const noexcept {
        size_type head = head_.load(std::memory_order_acquire);
        size_type tail = tail_.load(std::memory_order_acquire);
        return (tail >= head) ? (tail - head) : 0;
    }

    // 获取容量
    size_type capacity() const noexcept {
        return capacity_;
    }

private:
    // 将容量向上取整到 2 的幂（便于使用位运算取模）
    static constexpr size_type round_up_power_of_two(size_type n) {
        if (n == 0) return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        if constexpr (sizeof(size_type) > 4) {
            n |= n >> 32;
        }
        return n + 1;
    }

    const size_type capacity_;      // 容量（2 的幂）
    const size_type mask_;          // 掩码（capacity_ - 1）
    
    std::atomic<size_type> head_;  // 消费者读取位置
    std::atomic<size_type> tail_;  // 生产者写入位置
    
    // 环形缓冲区：每个 slot 是 optional<T>，用于标记是否已写入
    std::vector<std::optional<T>> buffer_;
};

}  // namespace usn
