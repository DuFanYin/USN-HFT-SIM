// SPDX-License-Identifier: MIT
//
// io_uring 异步 I/O 封装
//
// 设计目标：
// - 封装 io_uring API
// - 支持异步网络 I/O
// - Zero-copy 模式支持

#pragma once

#ifdef __linux__
#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstddef>
#include <vector>
#include <functional>

namespace usn {

// io_uring 封装类
class IoUringWrapper {
public:
    explicit IoUringWrapper(unsigned entries = 256)
        : ring_()
        , initialized_(false) {
        if (io_uring_queue_init(entries, &ring_, 0) == 0) {
            initialized_ = true;
        }
    }
    
    ~IoUringWrapper() {
        if (initialized_) {
            io_uring_queue_exit(&ring_);
        }
    }
    
    IoUringWrapper(const IoUringWrapper&) = delete;
    IoUringWrapper& operator=(const IoUringWrapper&) = delete;
    
    // 检查是否初始化成功
    bool is_initialized() const noexcept {
        return initialized_;
    }
    
    // 提交异步接收请求
    bool submit_recv(int fd, void* buf, size_t len, unsigned flags = 0) {
        if (!initialized_) {
            return false;
        }
        
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_recv(sqe, fd, buf, len, flags);
        io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
        
        return io_uring_submit(&ring_) >= 0;
    }
    
    // 提交异步发送请求
    bool submit_send(int fd, const void* buf, size_t len, unsigned flags = 0) {
        if (!initialized_) {
            return false;
        }
        
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            return false;
        }
        
        io_uring_prep_send(sqe, fd, buf, len, flags);
        io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
        
        return io_uring_submit(&ring_) >= 0;
    }
    
    // 等待完成事件
    int wait_completions(
        std::vector<struct io_uring_cqe*>& cqes,
        unsigned min_completions = 1,
        unsigned max_completions = 64
    ) {
        if (!initialized_) {
            return 0;
        }
        
        cqes.clear();
        cqes.reserve(max_completions);
        
        unsigned count = 0;
        struct io_uring_cqe* cqe;
        
        while (count < max_completions) {
            int ret = io_uring_wait_cqe(&ring_, &cqe);
            if (ret < 0) {
                break;
            }
            
            cqes.push_back(cqe);
            count++;
            
            if (count >= min_completions) {
                // 检查是否有更多完成事件（非阻塞）
                while (count < max_completions) {
                    ret = io_uring_peek_cqe(&ring_, &cqe);
                    if (ret < 0) {
                        break;
                    }
                    cqes.push_back(cqe);
                    count++;
                }
                break;
            }
        }
        
        // 标记所有完成事件为已处理
        for (auto* cqe : cqes) {
            io_uring_cqe_seen(&ring_, cqe);
        }
        
        return count;
    }
    
    // 非阻塞等待完成事件
    int peek_completions(
        std::vector<struct io_uring_cqe*>& cqes,
        unsigned max_completions = 64
    ) {
        if (!initialized_) {
            return 0;
        }
        
        cqes.clear();
        cqes.reserve(max_completions);
        
        unsigned count = 0;
        struct io_uring_cqe* cqe;
        
        while (count < max_completions) {
            int ret = io_uring_peek_cqe(&ring_, &cqe);
            if (ret < 0) {
                break;
            }
            
            cqes.push_back(cqe);
            count++;
        }
        
        // 标记为已处理
        for (auto* cqe : cqes) {
            io_uring_cqe_seen(&ring_, cqe);
        }
        
        return count;
    }
    
    // 获取 ring 指针（用于高级操作）
    struct io_uring* ring() noexcept {
        return &ring_;
    }

private:
    struct io_uring ring_;
    bool initialized_;
};

}  // namespace usn

#else
// 非 Linux 系统：提供空实现
namespace usn {
class IoUringWrapper {
public:
    explicit IoUringWrapper(unsigned = 256) {}
    bool is_initialized() const noexcept { return false; }
    bool submit_recv(int, void*, size_t, unsigned = 0) { return false; }
    bool submit_send(int, const void*, size_t, unsigned = 0) { return false; }
    int wait_completions(std::vector<void*>&, unsigned = 1, unsigned = 64) { return 0; }
    int peek_completions(std::vector<void*>&, unsigned = 64) { return 0; }
    void* ring() noexcept { return nullptr; }
};
}
#endif
