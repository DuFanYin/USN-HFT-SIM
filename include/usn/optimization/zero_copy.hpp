// SPDX-License-Identifier: MIT
//
// Zero-Copy 缓冲区管理
//
// 设计目标：
// - 使用 mmap 进行内存映射
// - 避免内核与用户空间之间的数据拷贝
// - 支持大页（huge pages）以提高性能
// - 与 Packet Ring Buffer 集成
//
// 技术方案：
// - mmap 匿名内存映射
// - 共享内存（shm_open）
// - 自定义内存分配器

#pragma once

#include <usn/core/packet.hpp>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>

namespace usn {

// Zero-Copy 缓冲区
class ZeroCopyBuffer {
public:
    // 创建指定大小的零拷贝缓冲区
    static std::unique_ptr<ZeroCopyBuffer> create(
        std::size_t size,
        bool use_huge_pages = false
    ) {
        void* ptr = nullptr;
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
        
        if (use_huge_pages) {
#ifdef MAP_HUGETLB
            // 尝试使用大页（2MB）- Linux 特有
            flags |= MAP_HUGETLB;
            ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, flags, -1, 0);
            
            if (ptr == MAP_FAILED) {
                // 大页分配失败，fallback 到普通页
                flags &= ~MAP_HUGETLB;
                ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, flags, -1, 0);
            }
#else
            // 不支持大页，使用普通页
            ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, flags, -1, 0);
#endif
        } else {
            ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, flags, -1, 0);
        }
        
        if (ptr == MAP_FAILED) {
            return nullptr;
        }
        
        // 建议内核不要交换这些页面（MLOCK 需要权限，这里只是建议）
#ifdef MADV_DONTDUMP
        madvise(ptr, size, MADV_DONTDUMP);
#elif defined(MADV_NOCORE)
        // macOS fallback
        madvise(ptr, size, MADV_NOCORE);
#endif
        
        return std::unique_ptr<ZeroCopyBuffer>(
            new ZeroCopyBuffer(ptr, size, use_huge_pages)
        );
    }
    
    ~ZeroCopyBuffer() {
        if (ptr_) {
            munmap(ptr_, size_);
        }
    }
    
    ZeroCopyBuffer(const ZeroCopyBuffer&) = delete;
    ZeroCopyBuffer& operator=(const ZeroCopyBuffer&) = delete;
    
    // 获取缓冲区指针
    uint8_t* data() noexcept {
        return static_cast<uint8_t*>(ptr_);
    }
    
    const uint8_t* data() const noexcept {
        return static_cast<const uint8_t*>(ptr_);
    }
    
    // 获取缓冲区大小
    std::size_t size() const noexcept {
        return size_;
    }
    
    // 检查是否使用大页
    bool uses_huge_pages() const noexcept {
        return use_huge_pages_;
    }
    
    // 锁定内存（防止交换到磁盘）
    // 注意：需要 CAP_IPC_LOCK 权限
    bool lock_memory() {
        return mlock(ptr_, size_) == 0;
    }
    
    // 解锁内存
    void unlock_memory() {
        munlock(ptr_, size_);
    }
    
    // 预取内存到缓存
    void prefetch(std::size_t offset = 0, std::size_t len = 0) const {
        if (len == 0) {
            len = size_;
        }
        __builtin_prefetch(
            static_cast<const char*>(ptr_) + offset,
            0,  // 读
            3   // 高时间局部性
        );
    }

private:
    ZeroCopyBuffer(void* ptr, std::size_t size, bool use_huge_pages)
        : ptr_(ptr)
        , size_(size)
        , use_huge_pages_(use_huge_pages) {}
    
    void* ptr_;
    std::size_t size_;
    bool use_huge_pages_;
};

// Zero-Copy 内存池（基于 mmap）
class ZeroCopyMemoryPool {
public:
    explicit ZeroCopyMemoryPool(
        std::size_t block_size,
        std::size_t num_blocks,
        bool use_huge_pages = false
    )
        : block_size_(block_size)
        , num_blocks_(num_blocks)
        , use_huge_pages_(use_huge_pages)
        , buffer_(ZeroCopyBuffer::create(block_size * num_blocks, use_huge_pages))
        , free_list_(num_blocks) {
        
        if (!buffer_) {
            throw std::runtime_error("Failed to create zero-copy buffer");
        }
        
        // 初始化 free list
        for (std::size_t i = 0; i < num_blocks - 1; ++i) {
            free_list_[i] = i + 1;
        }
        free_list_[num_blocks - 1] = SIZE_MAX;
        next_free_ = 0;
    }
    
    ZeroCopyMemoryPool(const ZeroCopyMemoryPool&) = delete;
    ZeroCopyMemoryPool& operator=(const ZeroCopyMemoryPool&) = delete;
    
    // 分配一个内存块
    uint8_t* allocate() {
        if (next_free_ == SIZE_MAX) {
            return nullptr;
        }
        
        std::size_t index = next_free_;
        next_free_ = free_list_[index];
        
        return buffer_->data() + (index * block_size_);
    }
    
    // 释放一个内存块
    void deallocate(uint8_t* ptr) {
        if (!ptr || ptr < buffer_->data() || 
            ptr >= buffer_->data() + buffer_->size()) {
            return;
        }
        
        std::size_t index = (ptr - buffer_->data()) / block_size_;
        free_list_[index] = next_free_;
        next_free_ = index;
    }
    
    std::size_t block_size() const noexcept {
        return block_size_;
    }
    
    std::size_t num_blocks() const noexcept {
        return num_blocks_;
    }
    
    bool uses_huge_pages() const noexcept {
        return use_huge_pages_;
    }

private:
    const std::size_t block_size_;
    const std::size_t num_blocks_;
    const bool use_huge_pages_;
    std::unique_ptr<ZeroCopyBuffer> buffer_;
    std::vector<std::size_t> free_list_;
    std::size_t next_free_;
};

}  // namespace usn
