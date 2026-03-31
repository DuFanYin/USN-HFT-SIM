// SPDX-License-Identifier: MIT
//
// TCP 单线程订单网关骨架：
// - 监听本地端口
// - 使用 epoll / 阻塞 I/O 单线程事件循环
// - 只打印收到的订单请求，不做真实撮合

#include "../common/messages.hpp"

#include <usn/core/packet_ring.hpp>
#include <usn/io/epoll_wrapper.hpp>
#include <usn/optimization/numa_utils.hpp>
#include <usn/optimization/zero_copy.hpp>
#include <usn/protocol/tcp_protocol.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <unordered_map>

namespace {

constexpr int kListenPort    = 9000;
constexpr int kBacklog = 128;
constexpr int kMaxEvents = 64;

struct ConnectionContext {
    usn::TcpConnection tcp{};
};

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

int main(int argc, char** argv) {
    usn::CpuAffinity::bind_to_cpu(0);
    int server_drop_at_s = 0;
    int backpressure_threshold = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        const int val = std::atoi(argv[i + 1]);
        if (key == "--server-drop-at-s") {
            server_drop_at_s = std::max(0, val);
        } else if (key == "--backpressure-threshold") {
            backpressure_threshold = std::max(0, val);
        }
    }

    int listen_fd = create_listen_socket();
    if (listen_fd < 0) {
        return 1;
    }

    std::cout << "[order_gateway] listening on TCP port " << kListenPort << " (single-thread loop)\n";

    usn::EpollWrapper epoll(kMaxEvents);
    if (epoll.fd() < 0) {
        std::perror("epoll_create1");
        ::close(listen_fd);
        return 1;
    }

    if (!epoll.add(listen_fd, EPOLLIN)) {
        std::perror("epoll add listen_fd");
        ::close(listen_fd);
        return 1;
    }

    // mmap 零拷贝内存池用于 ACK 发送缓冲区（避免热路径 malloc）
    constexpr std::size_t kAckBufSize = usn::TcpProtocol::kHeaderLen + 16;
    usn::ZeroCopyMemoryPool ack_pool(kAckBufSize, 128);
    ack_pool.allocate();  // prefault 第一页
    ack_pool.deallocate(ack_pool.allocate());

    // 收到的请求先入 ring，再统一处理（I/O 与处理解耦）
    usn::PacketRing req_ring(64);

    std::unordered_map<int, ConnectionContext> conn_ctx;
    std::vector<epoll_event> events;
    uint64_t req_recv_total = 0;
    uint64_t ack_sent_total = 0;
    uint64_t reject_total = 0;
    uint64_t backpressure_hits = 0;
    uint64_t accept_failures = 0;
    uint64_t unexpected_disconnects = 0;
    std::size_t active_conn_peak = 0;
    std::unordered_set<uint64_t> live_orders;
    auto start_time = std::chrono::steady_clock::now();
    auto last_report = std::chrono::steady_clock::now();

    while (true) {
        int n = epoll.wait(events, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("epoll_wait");
            break;
        }

        for (const auto& ev : events) {
            int fd = ev.data.fd;

            if (fd == listen_fd) {
                // 接受新连接
                sockaddr_in cli_addr{};
                socklen_t   cli_len = sizeof(cli_addr);
                int         conn_fd = ::accept4(listen_fd, reinterpret_cast<sockaddr*>(&cli_addr),
                                                &cli_len, SOCK_NONBLOCK);
                if (conn_fd < 0) {
                    std::perror("accept4");
                    ++accept_failures;
                    continue;
                }

                if (!epoll.add(conn_fd, EPOLLIN)) {
                    std::perror("epoll add conn_fd");
                    ::close(conn_fd);
                    continue;
                }

                ConnectionContext ctx{};
                ctx.tcp.local_ip = htonl(INADDR_LOOPBACK);
                ctx.tcp.remote_ip = htonl(INADDR_LOOPBACK);
                ctx.tcp.local_port = static_cast<uint16_t>(kListenPort);
                ctx.tcp.remote_port = ntohs(cli_addr.sin_port);
                ctx.tcp.recv_seq = 1;
                ctx.tcp.send_seq = 1;
                conn_ctx.emplace(conn_fd, ctx);
                active_conn_peak = std::max(active_conn_peak, conn_ctx.size());

                char ip[64]{};
                ::inet_ntop(AF_INET, &cli_addr.sin_addr, ip, sizeof(ip));
                std::cout << "[order_gateway] new connection from " << ip << ":" << ntohs(cli_addr.sin_port)
                          << " (fd=" << conn_fd << ")\n";

            } else {
                // --- I/O 阶段：读取原始数据，push 到 ring ---
                std::array<uint8_t, 2048> buf{};
                ssize_t nread = ::read(fd, buf.data(), buf.size());
                if (nread <= 0) {
                    if (nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        std::perror("read");
                    }
                    if (nread == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                        std::cout << "[order_gateway] connection closed, fd=" << fd << "\n";
                        ++unexpected_disconnects;
                        epoll.remove(fd);
                        ::close(fd);
                        conn_ctx.erase(fd);
                    }
                    continue;
                }

                usn::Packet tcp_packet(buf.data(), static_cast<std::size_t>(nread));
                tcp_packet.port = static_cast<uint16_t>(fd);  // 携带 fd 信息
                req_ring.try_push(tcp_packet);
            }
        }

        // --- 处理阶段：从 ring 取出请求，解析并回 ACK ---
        usn::Packet pkt;
        while (req_ring.try_pop(pkt)) {
            int fd = static_cast<int>(pkt.port);
            if (server_drop_at_s > 0 &&
                std::chrono::steady_clock::now() - start_time >= std::chrono::seconds(server_drop_at_s)) {
                epoll.remove(fd);
                ::close(fd);
                conn_ctx.erase(fd);
                continue;
            }

            usn::TcpHeader tcp_header{};
            const uint8_t* payload = nullptr;
            std::size_t payload_len = 0;
            if (!usn::TcpProtocol::parse(pkt, tcp_header, payload, payload_len)) {
                std::cerr << "[order_gateway] invalid tcp frame, fd=" << fd << "\n";
                continue;
            }
            if (payload_len < sizeof(usn::apps::OrderRequest)) {
                std::cerr << "[order_gateway] payload too small=" << payload_len << "\n";
                continue;
            }
            if (payload_len > sizeof(usn::apps::OrderRequest)) {
                std::cout << "[order_gateway] payload has trailing bytes, payload_len=" << payload_len
                          << " expected=" << sizeof(usn::apps::OrderRequest) << "\n";
            }

            usn::apps::OrderRequest req{};
            std::memcpy(&req, payload, sizeof(req));
            ++req_recv_total;
            bool accepted = true;
            if (req.action == usn::apps::OrderAction::New) {
                live_orders.insert(req.client_order_id);
            } else if (req.action == usn::apps::OrderAction::Cancel) {
                accepted = live_orders.erase(req.target_order_id) > 0;
            } else if (req.action == usn::apps::OrderAction::Replace) {
                accepted = live_orders.find(req.target_order_id) != live_orders.end();
            }
            if (!accepted) {
                ++reject_total;
            }
            if (backpressure_threshold > 0 &&
                static_cast<int>(req_ring.size()) >= backpressure_threshold) {
                ++backpressure_hits;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

            auto it = conn_ctx.find(fd);
            if (it != conn_ctx.end()) {
                it->second.tcp.recv_seq += static_cast<uint32_t>(payload_len);
                uint8_t* ack_buf = ack_pool.allocate();
                usn::apps::OrderAck ack{};
                ack.client_order_id = req.client_order_id;
                ack.server_order_id = req.client_order_id + 1000000;
                ack.accepted = accepted;
                usn::Packet ack_packet = usn::TcpProtocol::create_data(
                    it->second.tcp,
                    reinterpret_cast<const uint8_t*>(&ack),
                    sizeof(ack),
                    ack_buf
                );
                ssize_t nw = ::write(fd, ack_packet.data, ack_packet.len);
                ack_pool.deallocate(ack_packet.data);
                if (nw != static_cast<ssize_t>(ack_packet.len)) {
                    std::cerr << "[order_gateway] failed to send tcp ack, fd=" << fd << "\n";
                    ++unexpected_disconnects;
                } else {
                    ++ack_sent_total;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (now - last_report >= std::chrono::seconds(1)) {
                std::cout << "[order_gateway][metrics] req_recv_total=" << req_recv_total
                          << " ack_sent_total=" << ack_sent_total
                          << " active_conn=" << conn_ctx.size()
                          << " active_conn_peak=" << active_conn_peak
                          << " accept_failures=" << accept_failures
                          << " unexpected_disconnects=" << unexpected_disconnects
                          << " reject_total=" << reject_total
                          << " reject_rate=" << (req_recv_total ? (static_cast<double>(reject_total) / req_recv_total) : 0.0)
                          << " backpressure_hits=" << backpressure_hits << "\n";
                last_report = now;
            }
        }
    }

    ::close(listen_fd);
    return 0;
}

