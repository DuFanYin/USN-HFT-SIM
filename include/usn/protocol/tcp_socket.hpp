// SPDX-License-Identifier: MIT
//
// TCP socket wrapper for protocol apps.

#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>

namespace usn {

class TcpSocket {
public:
    TcpSocket() = default;
    explicit TcpSocket(int fd)
        : fd_(fd) {}

    ~TcpSocket() { close(); }

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& other) noexcept
        : fd_(other.fd_) {
        other.fd_ = -1;
    }

    TcpSocket& operator=(TcpSocket&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
        return *this;
    }

    static TcpSocket create_server(uint16_t port, int backlog = 128, bool nonblock = true) {
        const int type = nonblock ? (SOCK_STREAM | SOCK_NONBLOCK) : SOCK_STREAM;
        int fd = ::socket(AF_INET, type, 0);
        if (fd < 0) {
            return TcpSocket(-1);
        }

        int opt = 1;
        (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        (void)::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            return TcpSocket(-1);
        }
        if (::listen(fd, backlog) < 0) {
            ::close(fd);
            return TcpSocket(-1);
        }
        return TcpSocket(fd);
    }

    static TcpSocket connect_to(const std::string& ip, uint16_t port, int timeout_ms) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return TcpSocket(-1);
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            ::close(fd);
            return TcpSocket(-1);
        }

        // Set non-blocking for connect with timeout.
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            ::close(fd);
            return TcpSocket(-1);
        }
        if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            ::close(fd);
            return TcpSocket(-1);
        }

        int ret = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (ret < 0 && errno != EINPROGRESS) {
            ::close(fd);
            return TcpSocket(-1);
        }
        if (ret < 0) {
            // Connection in progress — wait with poll.
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int poll_ret = ::poll(&pfd, 1, timeout_ms);
            if (poll_ret <= 0) {
                ::close(fd);
                return TcpSocket(-1);
            }
            int so_error = 0;
            socklen_t elen = sizeof(so_error);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &elen) < 0 || so_error != 0) {
                ::close(fd);
                return TcpSocket(-1);
            }
        }

        // Restore blocking mode and set recv timeout.
        (void)::fcntl(fd, F_SETFL, flags);

        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        return TcpSocket(fd);
    }

    int accept(sockaddr_in* out_addr = nullptr, bool nonblock = true) const {
        if (fd_ < 0) {
            errno = EBADF;
            return -1;
        }
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        const int flags = nonblock ? SOCK_NONBLOCK : 0;
        int conn_fd = ::accept4(fd_, reinterpret_cast<sockaddr*>(&addr), &len, flags);
        if (conn_fd >= 0 && out_addr != nullptr) {
            *out_addr = addr;
        }
        return conn_fd;
    }

    static int accept_from(int listen_fd, sockaddr_in* out_addr = nullptr, bool nonblock = true) {
        if (listen_fd < 0) {
            errno = EBADF;
            return -1;
        }
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        const int flags = nonblock ? SOCK_NONBLOCK : 0;
        int conn_fd = ::accept4(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len, flags);
        if (conn_fd >= 0 && out_addr != nullptr) {
            *out_addr = addr;
        }
        return conn_fd;
    }

    ssize_t read(void* buf, std::size_t len) const {
        if (fd_ < 0) {
            errno = EBADF;
            return -1;
        }
        return ::read(fd_, buf, len);
    }

    ssize_t write(const void* data, std::size_t len) const {
        if (fd_ < 0) {
            errno = EBADF;
            return -1;
        }
        return ::write(fd_, data, len);
    }

    static ssize_t read_from(int fd, void* buf, std::size_t len) {
        if (fd < 0) {
            errno = EBADF;
            return -1;
        }
        return ::read(fd, buf, len);
    }

    static ssize_t write_to(int fd, const void* data, std::size_t len) {
        if (fd < 0) {
            errno = EBADF;
            return -1;
        }
        return ::write(fd, data, len);
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd() const noexcept { return fd_; }
    bool is_open() const noexcept { return fd_ >= 0; }
    int release() noexcept {
        const int out = fd_;
        fd_ = -1;
        return out;
    }

private:
    int fd_{-1};
};

}  // namespace usn
