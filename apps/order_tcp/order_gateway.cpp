// SPDX-License-Identifier: MIT
//
// TCP 单线程订单网关骨架：
// - 监听本地端口
// - 使用 epoll / 阻塞 I/O 单线程事件循环
// - 只打印收到的订单请求，不做真实撮合

#include <usn/apps/messages.hpp>

#include <usn/core/packet_ring.hpp>
#include <usn/core/tracing.hpp>
#include <usn/io/epoll_wrapper.hpp>
#include <usn/io/io_status.hpp>
#include <usn/optimization/numa_utils.hpp>
#include <usn/optimization/zero_copy.hpp>
#include <usn/protocol/tcp_protocol.hpp>
#include <usn/protocol/tcp_reassembly.hpp>
#include <usn/protocol/tcp_socket.hpp>
#include <usn/protocol/tcp_state_machine.hpp>

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
    usn::TcpReassemblyBuffer reassembly{};
};

int create_listen_socket() {
    auto sock = usn::TcpSocket::create_server(static_cast<uint16_t>(kListenPort), kBacklog, true);
    if (!sock.is_open()) {
        std::perror("socket");
        return -1;
    }
    return sock.release();
}

}  // namespace

int main(int argc, char** argv) {
    usn::CpuAffinity::bind_to_cpu(0);
    int server_drop_at_s = 0;
    int backpressure_threshold = 0;
    int trace_every = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        const int val = std::atoi(argv[i + 1]);
        if (key == "--server-drop-at-s") {
            server_drop_at_s = std::max(0, val);
        } else if (key == "--backpressure-threshold") {
            backpressure_threshold = std::max(0, val);
        } else if (key == "--trace-every") {
            trace_every = std::max(0, val);
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
    usn::Tracer tracer(usn::TraceConfig{trace_every > 0, static_cast<uint32_t>(std::max(1, trace_every))});

    std::unordered_map<int, ConnectionContext> conn_ctx;
    std::vector<epoll_event> events;
    uint64_t req_recv_total = 0;
    uint64_t ack_sent_total = 0;
    uint64_t reject_total = 0;
    uint64_t backpressure_hits = 0;
    uint64_t reorder_buffered_total = 0;
    uint64_t reorder_replayed_total = 0;
    uint64_t reorder_hole_events = 0;
    uint64_t reorder_hole_peak = 0;
    uint64_t reorder_hole_bytes_peak = 0;
    uint64_t accept_failures = 0;
    uint64_t unexpected_disconnects = 0;
    uint64_t keepalive_recv_total = 0;
    std::size_t active_conn_peak = 0;
    std::unordered_set<uint64_t> live_orders;
    auto start_time = std::chrono::steady_clock::now();
    auto last_report = std::chrono::steady_clock::now();

    usn::EpollWaitOptions wait_opts;
    wait_opts.timeout_ms = -1;

    while (true) {
        const auto wait_result = epoll.wait_with_options(events, wait_opts);
        const auto io_result = usn::to_unified_result(wait_result);
        if (io_result.status == usn::UnifiedIOStatus::SysError) {
            std::perror("epoll_wait");
            break;
        }
        if (io_result.status == usn::UnifiedIOStatus::Cancelled) {
            std::cout << "[order_gateway] epoll loop cancelled\n";
            break;
        }
        if (usn::classify_loop_control(io_result) != usn::LoopControl::ContinueWork) {
            continue;
        }

        for (const auto& ev : events) {
            int fd = ev.data.fd;

            if (fd == listen_fd) {
                // 接受新连接
                sockaddr_in cli_addr{};
                int conn_fd = usn::TcpSocket::accept_from(listen_fd, &cli_addr, true);
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
                ssize_t nread = usn::TcpSocket::read_from(fd, buf.data(), buf.size());
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

        auto process_order_payload = [&](int fd, ConnectionContext& ctx, const uint8_t* payload, std::size_t payload_len) {
            if (payload_len < sizeof(usn::apps::OrderRequest)) {
                std::cerr << "[order_gateway] payload too small=" << payload_len << "\n";
                return;
            }
            if (payload_len > sizeof(usn::apps::OrderRequest)) {
                std::cout << "[order_gateway] payload has trailing bytes, payload_len=" << payload_len
                          << " expected=" << sizeof(usn::apps::OrderRequest) << "\n";
            }

            usn::apps::OrderRequest req{};
            std::memcpy(&req, payload, sizeof(req));
            ++req_recv_total;
            ctx.tcp.recv_seq += static_cast<uint32_t>(payload_len);
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

            uint8_t* ack_buf = ack_pool.allocate();
            usn::apps::OrderAck ack{};
            ack.client_order_id = req.client_order_id;
            ack.server_order_id = req.client_order_id + 1000000;
            ack.accepted = accepted;
            usn::Packet ack_packet = usn::TcpProtocol::create_data(
                ctx.tcp,
                reinterpret_cast<const uint8_t*>(&ack),
                sizeof(ack),
                ack_buf
            );
            ssize_t nw = usn::TcpSocket::write_to(fd, ack_packet.data, ack_packet.len);
            ack_pool.deallocate(ack_packet.data);
            if (nw != static_cast<ssize_t>(ack_packet.len)) {
                std::cerr << "[order_gateway] failed to send tcp ack, fd=" << fd << "\n";
                ++unexpected_disconnects;
            } else {
                ++ack_sent_total;
                tracer.emit(
                    "order_gateway",
                    "tcp_tx_ack",
                    req.client_order_id,
                    static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()
                        ).count()
                    ),
                    req_ring.size(),
                    0
                );
            }
        };

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
            auto it = conn_ctx.find(fd);
            if (it == conn_ctx.end()) {
                continue;
            }
            if (!usn::TcpProtocol::parse(pkt, tcp_header, payload, payload_len) ||
                usn::TcpProtocol::is_malformed_header(tcp_header)) {
                std::cerr << "[order_gateway] invalid tcp frame, fd=" << fd << "\n";
                continue;
            }
            auto& conn = it->second.tcp;

            // Control path first: FIN/ACK driven state transition.
            if (payload_len == 0 && (tcp_header.has_fin() || tcp_header.has_ack())) {
                (void)usn::TcpStateMachine::handle_incoming(conn, tcp_header, payload_len);
                if (tcp_header.has_ack() && !tcp_header.has_fin()) {
                    ++keepalive_recv_total;
                }
                if (tcp_header.has_fin()) {
                    uint8_t* ack_buf = ack_pool.allocate();
                    usn::Packet ack_pkt = usn::TcpProtocol::create_ack(conn, conn.send_ack, ack_buf);
                    ssize_t ack_w = usn::TcpSocket::write_to(fd, ack_pkt.data, ack_pkt.len);
                    if (ack_w < 0) {
                        std::perror("write fin-ack");
                    }
                    ack_pool.deallocate(ack_pkt.data);

                    if (conn.state == usn::TcpState::CLOSE_WAIT &&
                        usn::TcpStateMachine::initiate_close(conn)) {
                        uint8_t* fin_buf = ack_pool.allocate();
                        usn::Packet fin_pkt = usn::TcpProtocol::create_fin(conn, fin_buf);
                        ssize_t fin_w = usn::TcpSocket::write_to(fd, fin_pkt.data, fin_pkt.len);
                        if (fin_w < 0) {
                            std::perror("write fin");
                        }
                        ack_pool.deallocate(fin_pkt.data);
                        conn.send_seq += 1;
                    }
                }
                if (conn.state == usn::TcpState::CLOSED) {
                    epoll.remove(fd);
                    ::close(fd);
                    conn_ctx.erase(fd);
                }
                continue;
            }

            // out-of-order payload 先缓存，等待缺口补齐后回放
            if (payload_len > 0 && tcp_header.seq_num != conn.recv_seq) {
                if (it->second.reassembly.insert(tcp_header.seq_num, payload, payload_len)) {
                    ++reorder_buffered_total;
                    const auto holes = it->second.reassembly.hole_count(conn.recv_seq);
                    const auto hole_bytes = it->second.reassembly.total_hole_bytes(conn.recv_seq);
                    if (holes > 0) {
                        ++reorder_hole_events;
                    }
                    reorder_hole_peak = std::max<uint64_t>(reorder_hole_peak, holes);
                    reorder_hole_bytes_peak = std::max<uint64_t>(reorder_hole_bytes_peak, hole_bytes);
                }
                continue;
            }
            process_order_payload(fd, it->second, payload, payload_len);

            std::vector<usn::TcpBufferedSegment> replay_segments;
            const std::size_t replayed = it->second.reassembly.pop_contiguous(conn.recv_seq, replay_segments);
            if (replayed > 0) {
                reorder_replayed_total += replayed;
                for (const auto& seg : replay_segments) {
                    process_order_payload(fd, it->second, seg.data.data(), seg.data.size());
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
                          << " backpressure_hits=" << backpressure_hits
                          << " reorder_buffered_total=" << reorder_buffered_total
                          << " reorder_replayed_bytes=" << reorder_replayed_total
                          << " reorder_hole_events=" << reorder_hole_events
                          << " reorder_hole_peak=" << reorder_hole_peak
                          << " reorder_hole_bytes_peak=" << reorder_hole_bytes_peak
                          << " keepalive_recv_total=" << keepalive_recv_total << "\n";
                last_report = now;
            }
        }
    }

    ::close(listen_fd);
    return 0;
}

