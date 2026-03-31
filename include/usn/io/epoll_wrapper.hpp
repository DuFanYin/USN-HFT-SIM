// SPDX-License-Identifier: MIT
//
// Epoll 事件驱动封装
//
// 设计目标：
// - 封装 epoll API
// - 支持边缘触发（ET）和水平触发（LT）
// - 与用户空间协议栈集成

#pragma once

#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>
#include <functional>
#include <sys/epoll.h>

namespace usn {

// Epoll 事件类型（显式指定底层类型，避免与系统宏不兼容）
enum class EpollEventType : std::uint32_t {
    READ         = static_cast<std::uint32_t>(EPOLLIN),
    WRITE        = static_cast<std::uint32_t>(EPOLLOUT),
    ERROR        = static_cast<std::uint32_t>(EPOLLERR),
    HANGUP       = static_cast<std::uint32_t>(EPOLLHUP),
    EDGE_TRIGGER = static_cast<std::uint32_t>(EPOLLET),
    ONESHOT      = static_cast<std::uint32_t>(EPOLLONESHOT)
};

// Epoll 事件回调函数类型
using EpollCallback = std::function<void(int fd, uint32_t events)>;

enum class EpollWaitStatus {
    Ok,
    Timeout,
    Cancelled,
    SysError
};

struct EpollWaitOptions {
    // <0: wait indefinitely, 0: immediate, >0: bounded wait
    int timeout_ms{-1};
    const std::atomic<bool>* cancel_flag{nullptr};
    int poll_interval_ms{5};
};

struct EpollWaitResult {
    int count{0};
    int error{0};
    EpollWaitStatus status{EpollWaitStatus::Ok};

    bool success() const noexcept {
        return status != EpollWaitStatus::SysError;
    }
};

// Epoll 封装类
class EpollWrapper {
public:
    explicit EpollWrapper(int max_events = 64)
        : epoll_fd_(-1)
        , max_events_(max_events) {
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    }
    
    ~EpollWrapper() {
        if (epoll_fd_ >= 0) {
            close(epoll_fd_);
        }
    }
    
    EpollWrapper(const EpollWrapper&) = delete;
    EpollWrapper& operator=(const EpollWrapper&) = delete;
    
    // 添加文件描述符到 epoll
    bool add(int fd, uint32_t events, void* user_data = nullptr) {
        struct epoll_event ev;
        ev.events = events;
        ev.data.ptr = user_data;
        ev.data.fd = fd;
        
        return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
    }
    
    // 修改文件描述符的事件
    bool modify(int fd, uint32_t events, void* user_data = nullptr) {
        struct epoll_event ev;
        ev.events = events;
        ev.data.ptr = user_data;
        ev.data.fd = fd;
        
        return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
    }
    
    // 从 epoll 中删除文件描述符
    bool remove(int fd) {
        return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
    }
    
    // 等待事件（阻塞）
    int wait(std::vector<struct epoll_event>& events, int timeout_ms = -1) {
        EpollWaitOptions options;
        options.timeout_ms = timeout_ms;
        const auto result = wait_with_options(events, options);
        if (result.status == EpollWaitStatus::SysError) {
            errno = result.error;
            return -1;
        }
        return result.count;
    }

    EpollWaitResult wait_with_options(
        std::vector<struct epoll_event>& events,
        const EpollWaitOptions& options
    ) {
        events.resize(max_events_);
        if (options.cancel_flag != nullptr && options.cancel_flag->load(std::memory_order_acquire)) {
            events.clear();
            return {0, 0, EpollWaitStatus::Cancelled};
        }

        auto remaining_ms = options.timeout_ms;
        while (true) {
            int wait_ms = options.timeout_ms;
            if (options.timeout_ms < 0) {
                wait_ms = (options.poll_interval_ms > 0) ? options.poll_interval_ms : 1;
            } else if (options.timeout_ms > 0) {
                if (remaining_ms <= 0) {
                    events.clear();
                    return {0, 0, EpollWaitStatus::Timeout};
                }
                const int interval = (options.poll_interval_ms > 0) ? options.poll_interval_ms : 1;
                wait_ms = std::min(interval, remaining_ms);
                remaining_ms -= wait_ms;
            }

            const int num_events = epoll_wait(epoll_fd_, events.data(), max_events_, wait_ms);
            if (num_events > 0) {
                events.resize(num_events);
                return {num_events, 0, EpollWaitStatus::Ok};
            }
            if (num_events == 0) {
                if (options.cancel_flag != nullptr && options.cancel_flag->load(std::memory_order_acquire)) {
                    events.clear();
                    return {0, 0, EpollWaitStatus::Cancelled};
                }
                if (options.timeout_ms == 0) {
                    events.clear();
                    return {0, 0, EpollWaitStatus::Timeout};
                }
                if (options.timeout_ms > 0 && remaining_ms <= 0) {
                    events.clear();
                    return {0, 0, EpollWaitStatus::Timeout};
                }
                // bounded or infinite mode with budget remaining: keep polling
                continue;
            }

            if (errno == EINTR) {
                if (options.cancel_flag != nullptr && options.cancel_flag->load(std::memory_order_acquire)) {
                    events.clear();
                    return {0, 0, EpollWaitStatus::Cancelled};
                }
                continue;
            }

            const int err = errno;
            events.clear();
            return {-1, err, EpollWaitStatus::SysError};
        }
    }
    
    // 等待事件（非阻塞）
    int wait_nonblock(std::vector<struct epoll_event>& events) {
        return wait(events, 0);
    }
    
    // 获取 epoll 文件描述符
    int fd() const noexcept {
        return epoll_fd_;
    }
    
    // 事件循环
    void event_loop(
        std::function<void(int fd, uint32_t events)> callback,
        int timeout_ms = -1
    ) {
        std::vector<struct epoll_event> events;
        
        while (true) {
            int num_events = wait(events, timeout_ms);
            
            if (num_events < 0) {
                if (errno == EINTR) {
                    continue;  // 被信号中断，继续
                }
                break;  // 错误
            }
            
            for (const auto& ev : events) {
                callback(ev.data.fd, ev.events);
            }
        }
    }

private:
    int epoll_fd_;
    int max_events_;
};

}  // namespace usn
