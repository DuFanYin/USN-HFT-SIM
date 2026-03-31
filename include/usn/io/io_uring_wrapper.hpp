// SPDX-License-Identifier: MIT
//
// io_uring 异步 I/O 封装
//
// 设计目标：
// - 封装 io_uring API
// - 支持异步网络 I/O
// - Zero-copy 模式支持

#pragma once

#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <atomic>
#include <cstddef>
#include <algorithm>
#include <cerrno>
#include <vector>
#include <functional>

namespace usn {

enum class IoUringWaitStatus {
    Ok,
    Timeout,
    Cancelled,
    NotInitialized,
    SysError
};

struct IoUringWaitOptions {
    // <0: wait indefinitely, 0: immediate, >0: bounded wait
    int timeout_ms{-1};
    const std::atomic<bool>* cancel_flag{nullptr};
    int poll_interval_ms{1};
};

struct IoUringWaitResult {
    int count{0};
    int error{0};
    IoUringWaitStatus status{IoUringWaitStatus::Ok};

    bool success() const noexcept {
        return status != IoUringWaitStatus::SysError && status != IoUringWaitStatus::NotInitialized;
    }
};

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
        IoUringWaitOptions options;
        const auto result = wait_completions_with_options(cqes, min_completions, max_completions, options);
        if (result.status == IoUringWaitStatus::SysError) {
            errno = result.error;
            return -1;
        }
        if (result.status == IoUringWaitStatus::NotInitialized) {
            return 0;
        }
        return result.count;
    }

    IoUringWaitResult wait_completions_with_options(
        std::vector<struct io_uring_cqe*>& cqes,
        unsigned min_completions,
        unsigned max_completions,
        const IoUringWaitOptions& options
    ) {
        if (!initialized_) {
            cqes.clear();
            return {0, 0, IoUringWaitStatus::NotInitialized};
        }

        cqes.clear();
        cqes.reserve(max_completions);

        if (options.cancel_flag != nullptr && options.cancel_flag->load(std::memory_order_acquire)) {
            return {0, 0, IoUringWaitStatus::Cancelled};
        }

        unsigned count = 0;
        struct io_uring_cqe* cqe;

        auto remaining_ms = options.timeout_ms;
        while (count < max_completions) {
            int ret = 0;
            if (count < min_completions) {
                if (options.timeout_ms == 0) {
                    ret = io_uring_peek_cqe(&ring_, &cqe);
                } else if (options.timeout_ms > 0) {
                    __kernel_timespec ts{};
                    int step_ms = options.poll_interval_ms > 0 ? options.poll_interval_ms : 1;
                    if (remaining_ms <= 0) {
                        return {static_cast<int>(count), 0, IoUringWaitStatus::Timeout};
                    }
                    step_ms = std::min(step_ms, remaining_ms);
                    remaining_ms -= step_ms;
                    ts.tv_sec = step_ms / 1000;
                    ts.tv_nsec = static_cast<long>(step_ms % 1000) * 1000000L;
                    ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
                } else {
                    ret = io_uring_wait_cqe(&ring_, &cqe);
                }
            } else {
                ret = io_uring_peek_cqe(&ring_, &cqe);
            }

            if (ret < 0) {
                if (ret == -EINTR) {
                    if (options.cancel_flag != nullptr && options.cancel_flag->load(std::memory_order_acquire)) {
                        return {static_cast<int>(count), 0, IoUringWaitStatus::Cancelled};
                    }
                    continue;
                }
                if (ret == -ETIME || ret == -EAGAIN) {
                    if (options.cancel_flag != nullptr && options.cancel_flag->load(std::memory_order_acquire)) {
                        return {static_cast<int>(count), 0, IoUringWaitStatus::Cancelled};
                    }
                    if (count == 0) {
                        return {0, 0, (options.timeout_ms >= 0) ? IoUringWaitStatus::Timeout : IoUringWaitStatus::Ok};
                    }
                    break;
                }
                return {static_cast<int>(count), -ret, IoUringWaitStatus::SysError};
            }

            cqes.push_back(cqe);
            count++;

            if (options.cancel_flag != nullptr && options.cancel_flag->load(std::memory_order_acquire)) {
                break;
            }
        }

        // 标记所有完成事件为已处理
        for (auto* cqe : cqes) {
            io_uring_cqe_seen(&ring_, cqe);
        }

        if (options.cancel_flag != nullptr && options.cancel_flag->load(std::memory_order_acquire) && count == 0) {
            return {0, 0, IoUringWaitStatus::Cancelled};
        }
        if (count == 0 && options.timeout_ms >= 0) {
            return {0, 0, IoUringWaitStatus::Timeout};
        }
        return {static_cast<int>(count), 0, IoUringWaitStatus::Ok};
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
