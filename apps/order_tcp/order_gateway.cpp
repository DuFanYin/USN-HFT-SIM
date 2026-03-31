// SPDX-License-Identifier: MIT
//
// TCP 单线程订单网关骨架：
// - 监听本地端口
// - 使用 epoll / 阻塞 I/O 单线程事件循环
// - 只打印收到的订单请求，不做真实撮合

#include "../common/messages.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace {

constexpr int kListenPort    = 9000;
constexpr int kBacklog       = 128;
constexpr int kMaxEvents     = 64;

int create_listen_socket() {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(kListenPort);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(fd);
        return -1;
    }

    if (::listen(fd, kBacklog) < 0) {
        std::perror("listen");
        ::close(fd);
        return -1;
    }

    return fd;
}

}  // namespace

int main() {
    int listen_fd = create_listen_socket();
    if (listen_fd < 0) {
        return 1;
    }

    std::cout << "[order_gateway] listening on TCP port " << kListenPort << " (single-thread loop)\n";

    int epfd = ::epoll_create1(0);
    if (epfd < 0) {
        std::perror("epoll_create1");
        ::close(listen_fd);
        return 1;
    }

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = listen_fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        std::perror("epoll_ctl listen_fd");
        ::close(listen_fd);
        ::close(epfd);
        return 1;
    }

    epoll_event events[kMaxEvents];

    while (true) {
        int n = ::epoll_wait(epfd, events, kMaxEvents, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                // 接受新连接
                sockaddr_in cli_addr{};
                socklen_t   cli_len = sizeof(cli_addr);
                int         conn_fd = ::accept4(listen_fd, reinterpret_cast<sockaddr*>(&cli_addr),
                                                &cli_len, SOCK_NONBLOCK);
                if (conn_fd < 0) {
                    std::perror("accept4");
                    continue;
                }

                epoll_event conn_ev{};
                conn_ev.events  = EPOLLIN;
                conn_ev.data.fd = conn_fd;
                if (::epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &conn_ev) < 0) {
                    std::perror("epoll_ctl conn_fd");
                    ::close(conn_fd);
                    continue;
                }

                char ip[64]{};
                ::inet_ntop(AF_INET, &cli_addr.sin_addr, ip, sizeof(ip));
                std::cout << "[order_gateway] new connection from " << ip << ":" << ntohs(cli_addr.sin_port)
                          << " (fd=" << conn_fd << ")\n";

            } else {
                // 读取订单请求（简化：一条消息按固定大小读取）
                usn::apps::OrderRequest req{};
                ssize_t                 nread = ::read(fd, &req, sizeof(req));
                if (nread <= 0) {
                    if (nread < 0) {
                        std::perror("read");
                    }
                    std::cout << "[order_gateway] connection closed, fd=" << fd << "\n";
                    ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    ::close(fd);
                    continue;
                }

                std::cout << "[order_gateway] received order: client_order_id=" << req.client_order_id
                          << " instr=" << req.instrument_id << " side=" << static_cast<int>(req.side)
                          << " px=" << req.price << " qty=" << req.quantity << "\n";

                // 骨架阶段先不回 ACK，只是展示收到的订单
            }
        }
    }

    ::close(epfd);
    ::close(listen_fd);
    return 0;
}

