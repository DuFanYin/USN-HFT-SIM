// SPDX-License-Identifier: MIT
//
// Packet Ring Buffer - 高性能数据包环形缓冲区
//
// 设计目标：
// - 固定大小的环形缓冲区
// - 批量操作支持（batch enqueue/dequeue）
// - 零拷贝（指针传递，不拷贝数据）
// - Cache-aligned 数据结构
// - 预分配内存池（避免运行时分配）
//
// 使用场景：
// - 网络数据包接收/发送缓冲区
// - 高性能数据包处理流水线

#pragma once

#include <usn/core/packet.hpp>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace usn {

// Packet Ring Buffer - 单生产者单消费者（SPSC）版本
// 对于多生产者场景，可以使用多个 ring buffer 或升级到 MPSC
class PacketRing {
public:
    using size_type = std::size_t;
    
    explicit PacketRing(size_type capacity)
        : capacity_(round_up_power_of_two(capacity))
        , mask_(capacity_ - 1)
        , write_pos_(0)
        , read_pos_(0)
        , packets_(capacity_) {
        // 初始化所有数据包
        for (auto& pkt : packets_) {
            pkt.reset();
        }
    }
    
    PacketRing(const PacketRing&) = delete;
    PacketRing& operator=(const PacketRing&) = delete;
    
    // 单生产者：push 单个数据包
    // 返回 true 成功，false 队列满
    bool try_push(const Packet& packet) {
        size_type write = write_pos_.load(std::memory_order_relaxed);
        size_type read = read_pos_.load(std::memory_order_acquire);
        
        if (write - read >= capacity_) {
            return false;  // 队列满
        }
        
        packets_[write & mask_] = packet;
        write_pos_.store(write + 1, std::memory_order_release);
        
        return true;
    }
    
    // 单生产者：批量 push
    // 返回实际 push 的数量
    size_type try_push_batch(std::span<const Packet> packets) {
        size_type write = write_pos_.load(std::memory_order_relaxed);
        size_type read = read_pos_.load(std::memory_order_acquire);
        
        size_type available = capacity_ - (write - read);
        size_type to_push = std::min(packets.size(), available);
        
        if (to_push == 0) {
            return 0;
        }
        
        for (size_type i = 0; i < to_push; ++i) {
            packets_[(write + i) & mask_] = packets[i];
        }
        
        write_pos_.store(write + to_push, std::memory_order_release);
        return to_push;
    }
    
    // 单消费者：pop 单个数据包
    // 返回 true 成功，false 队列空
    bool try_pop(Packet& packet) {
        size_type read = read_pos_.load(std::memory_order_relaxed);
        size_type write = write_pos_.load(std::memory_order_acquire);
        
        if (read >= write) {
            return false;  // 队列空
        }
        
        packet = packets_[read & mask_];
        packets_[read & mask_].reset();  // 清理
        read_pos_.store(read + 1, std::memory_order_release);
        
        return true;
    }
    
    // 单消费者：批量 pop
    // 返回实际 pop 的数量
    size_type try_pop_batch(std::span<Packet> packets) {
        size_type read = read_pos_.load(std::memory_order_relaxed);
        size_type write = write_pos_.load(std::memory_order_acquire);
        
        size_type available = write - read;
        size_type to_pop = std::min(packets.size(), available);
        
        if (to_pop == 0) {
            return 0;
        }
        
        for (size_type i = 0; i < to_pop; ++i) {
            packets[i] = packets_[(read + i) & mask_];
            packets_[(read + i) & mask_].reset();  // 清理
        }
        
        read_pos_.store(read + to_pop, std::memory_order_release);
        return to_pop;
    }
    
    // 检查队列是否为空
    bool empty() const noexcept {
        size_type read = read_pos_.load(std::memory_order_acquire);
        size_type write = write_pos_.load(std::memory_order_acquire);
        return read >= write;
    }
    
    // 获取队列大小
    size_type size() const noexcept {
        size_type read = read_pos_.load(std::memory_order_acquire);
        size_type write = write_pos_.load(std::memory_order_acquire);
        return (write >= read) ? (write - read) : 0;
    }
    
    // 获取可用空间
    size_type available() const noexcept {
        return capacity_ - size();
    }
    
    // 获取容量
    size_type capacity() const noexcept {
        return capacity_;
    }
    
private:
    // 将容量向上取整到 2 的幂
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
    
    // Cache-aligned 的原子变量，避免 false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_type> write_pos_;  // 写入位置
    alignas(CACHE_LINE_SIZE) std::atomic<size_type> read_pos_;   // 读取位置
    
    // 数据包缓冲区（每个 Packet 已经是 cache-aligned）
    std::vector<Packet> packets_;
};

}  // namespace usn
