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
#include <cerrno>
#include <cstdint>
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
        events.resize(max_events_);
        int num_events = epoll_wait(epoll_fd_, events.data(), max_events_, timeout_ms);
        if (num_events > 0) {
            events.resize(num_events);
        } else {
            events.clear();
        }
        return num_events;
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
