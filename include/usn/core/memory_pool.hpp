// SPDX-License-Identifier: MIT
//
// Memory Pool - 预分配内存池
//
// 设计目标：
// - 预分配固定大小的内存块
// - 避免运行时 malloc/free
// - 减少内存碎片
// - 提高分配性能
//
// 用于数据包缓冲区管理

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace usn {

// 简单的内存池：预分配固定大小的内存块
class MemoryPool {
public:
    explicit MemoryPool(std::size_t block_size, std::size_t num_blocks)
        : block_size_(block_size)
        , num_blocks_(num_blocks)
        , pool_(block_size * num_blocks)
        , free_list_(num_blocks) {
        // 初始化 free list：每个块指向下一个
        for (std::size_t i = 0; i < num_blocks - 1; ++i) {
            free_list_[i] = i + 1;
        }
        free_list_[num_blocks - 1] = SIZE_MAX;  // 最后一个块指向无效
        next_free_ = 0;
    }
    
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    // 分配一个内存块
    // 返回指向内存块的指针，失败返回 nullptr
    uint8_t* allocate() {
        if (next_free_ == SIZE_MAX) {
            return nullptr;  // 池已耗尽
        }
        
        std::size_t index = next_free_;
        next_free_ = free_list_[index];
        
        return pool_.data() + (index * block_size_);
    }
    
    // 释放一个内存块
    // ptr 必须是从这个池分配的内存
    void deallocate(uint8_t* ptr) {
        if (ptr < pool_.data() || ptr >= pool_.data() + pool_.size()) {
            return;  // 无效指针
        }
        
        std::size_t index = (ptr - pool_.data()) / block_size_;
        free_list_[index] = next_free_;
        next_free_ = index;
    }
    
    // 获取块大小
    std::size_t block_size() const noexcept {
        return block_size_;
    }
    
    // 获取总块数
    std::size_t num_blocks() const noexcept {
        return num_blocks_;
    }
    
    // 获取可用块数（近似值）
    std::size_t available() const noexcept {
        std::size_t count = 0;
        std::size_t current = next_free_;
        while (current != SIZE_MAX && count < num_blocks_) {
            count++;
            current = free_list_[current];
        }
        return count;
    }

private:
    const std::size_t block_size_;
    const std::size_t num_blocks_;
    std::vector<uint8_t> pool_;           // 内存池
    std::vector<std::size_t> free_list_;  // 空闲列表
    std::size_t next_free_;               // 下一个空闲块索引
};

}  // namespace usn
