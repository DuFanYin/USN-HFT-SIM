// SPDX-License-Identifier: MIT
//
// TCP 订单客户端骨架：
// - 连接到本地订单网关
// - 周期性发送简单的 OrderRequest

#include <usn/apps/messages.hpp>

#include <usn/core/memory_pool.hpp>
#include <usn/core/tracing.hpp>
#include <usn/optimization/numa_utils.hpp>
#include <usn/protocol/tcp_congestion.hpp>
#include <usn/protocol/tcp_protocol.hpp>
#include <usn/protocol/tcp_retransmission.hpp>
#include <usn/protocol/tcp_socket.hpp>
#include <usn/protocol/tcp_state_machine.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr int         kGatewayPort = 9000;
constexpr uint16_t    kClientPort  = 9101;
std::atomic<bool> g_running{true};

void handle_signal(int) {
    g_running.store(false, std::memory_order_release);
}

uint64_t percentile_us(std::vector<uint64_t> samples, double q) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t idx = static_cast<std::size_t>(q * static_cast<double>(samples.size() - 1));
    return samples[idx];
}

enum class ClientState {
    CONNECTING,
    ACTIVE,
    RETRYING,
    STOPPED,
};

const char* state_name(ClientState s) {
    switch (s) {
        case ClientState::CONNECTING: return "CONNECTING";
        case ClientState::ACTIVE: return "ACTIVE";
        case ClientState::RETRYING: return "RETRYING";
        case ClientState::STOPPED: return "STOPPED";
    }
    return "UNKNOWN";
}

int connect_to_gateway(const std::string& gateway_ip, int timeout_ms) {
    auto sock = usn::TcpSocket::connect_to(gateway_ip, static_cast<uint16_t>(kGatewayPort), timeout_ms);
    if (!sock.is_open()) {
        std::perror("connect");
        return -1;
    }
    return sock.release();
}

}  // namespace

int main(int argc, char** argv) {
    usn::CpuAffinity::bind_to_cpu(1);
    ::signal(SIGINT, handle_signal);
    ::signal(SIGTERM, handle_signal);

    int qps = 2;
    int burst = 1;
    int disconnect_interval_s = 0;
    int timeout_ms = 1000;
    int cancel_ratio = 0;
    int replace_ratio = 0;
    int trace_every = 0;
    std::string gateway_ip = "127.0.0.1";
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        const int val = std::atoi(argv[i + 1]);
        if (key == "--qps") {
            qps = std::max(1, val);
        } else if (key == "--burst") {
            burst = std::max(1, val);
        } else if (key == "--disconnect-interval-s") {
            disconnect_interval_s = std::max(0, val);
        } else if (key == "--timeout-ms") {
            timeout_ms = std::max(1, val);
        } else if (key == "--cancel-ratio") {
            cancel_ratio = std::clamp(val, 0, 100);
        } else if (key == "--replace-ratio") {
            replace_ratio = std::clamp(val, 0, 100);
        } else if (key == "--trace-every") {
            trace_every = std::max(0, val);
        } else if (key == "--gateway-ip") {
            gateway_ip = argv[i + 1];
        }
    }

    ClientState state = ClientState::CONNECTING;
    std::cout << "[order_client][state] " << state_name(state) << "\n";
    int fd = connect_to_gateway(gateway_ip, timeout_ms);
    if (fd < 0) {
        state = ClientState::STOPPED;
        std::cout << "[order_client][state] " << state_name(state) << "\n";
        return 1;
    }
    state = ClientState::ACTIVE;
    std::cout << "[order_client][state] " << state_name(state) << "\n";

    std::cout << "[order_client] connected to " << gateway_ip << ":" << kGatewayPort << "\n";

    // 预分配内存池：每块足够容纳 TCP header(20) + OrderRequest
    constexpr std::size_t kPacketBufSize = usn::TcpProtocol::kHeaderLen + sizeof(usn::apps::OrderRequest) + 16;
    usn::MemoryPool pool(kPacketBufSize, 64);

    usn::TcpConnection conn{};
    conn.local_ip = htonl(INADDR_LOOPBACK);
    conn.remote_ip = htonl(INADDR_LOOPBACK);
    conn.local_port = kClientPort;
    conn.remote_port = static_cast<uint16_t>(kGatewayPort);
    conn.send_seq = 1;
    conn.recv_seq = 1;
    usn::Tracer tracer(usn::TraceConfig{trace_every > 0, static_cast<uint32_t>(std::max(1, trace_every))});

    uint64_t order_id = 1;
    uint64_t req_sent_total = 0;
    uint64_t ack_recv_total = 0;
    uint64_t ack_timeout_total = 0;
    uint64_t reconnect_total = 0;
    uint64_t send_blocked_total = 0;
    uint64_t retransmit_total = 0;
    uint64_t retransmit_failed_total = 0;
    uint64_t retransmit_drop_total = 0;
    uint64_t new_total = 0;
    uint64_t cancel_total = 0;
    uint64_t replace_total = 0;
    std::vector<uint64_t> rtt_us_samples;
    rtt_us_samples.reserve(1 << 14);
    std::vector<uint64_t> rtt_new;
    std::vector<uint64_t> rtt_cancel;
    std::vector<uint64_t> rtt_replace;
    std::unordered_map<uint64_t, usn::apps::OrderRequest> order_book_shadow;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> pct(1, 100);
    usn::TcpRetransmissionQueue retrans_q;
    usn::TcpRtoEstimator rto_estimator;
    usn::TcpCongestionController cc;
    std::unordered_map<uint32_t, std::vector<uint8_t>> outstanding_segments;
    auto last_report = std::chrono::steady_clock::now();
    auto last_activity = std::chrono::steady_clock::now();
    uint64_t keepalive_sent_total = 0;
    uint64_t cwnd_blocked_total = 0;
    uint64_t congestion_loss_total = 0;
    constexpr uint64_t kKeepaliveIntervalMs = 1000;
    const auto burst_interval = std::chrono::microseconds((1000000 * burst) / std::max(1, qps));
    auto last_disconnect = std::chrono::steady_clock::now();

    while (g_running.load(std::memory_order_acquire)) {
        if (disconnect_interval_s > 0 &&
            std::chrono::steady_clock::now() - last_disconnect >= std::chrono::seconds(disconnect_interval_s)) {
            ::close(fd);
            state = ClientState::RETRYING;
            std::cout << "[order_client][state] " << state_name(state) << "\n";
            fd = connect_to_gateway(gateway_ip, timeout_ms);
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            ++reconnect_total;
            cc = usn::TcpCongestionController{};
            state = ClientState::ACTIVE;
            std::cout << "[order_client][state] " << state_name(state) << "\n";
            last_disconnect = std::chrono::steady_clock::now();
        }

        const auto now_ms_for_ka = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        const auto last_activity_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                last_activity.time_since_epoch()
            ).count()
        );
        if (usn::TcpStateMachine::should_send_keepalive(now_ms_for_ka, last_activity_ms, kKeepaliveIntervalMs)) {
            uint8_t* ka_buf = pool.allocate();
            usn::Packet ka = usn::TcpStateMachine::create_keepalive(conn, ka_buf);
            ssize_t kw = usn::TcpSocket::write_to(fd, ka.data, ka.len);
            pool.deallocate(ka.data);
            if (kw == static_cast<ssize_t>(ka.len)) {
                ++keepalive_sent_total;
                last_activity = std::chrono::steady_clock::now();
            }
        }
        for (int i = 0; i < burst && g_running.load(std::memory_order_acquire); ++i) {
        usn::apps::OrderRequest req{};
        req.client_order_id = order_id++;
        req.target_order_id = 0;
        req.instrument_id   = 10001;
        req.action          = usn::apps::OrderAction::New;
        req.side            = (order_id % 2 == 0) ? usn::apps::Side::Buy : usn::apps::Side::Sell;
        req.price           = 100000 + static_cast<uint32_t>(order_id % 100);  // 1000.00x
        req.quantity        = 1;
        const int p = pct(rng);
        if (p <= cancel_ratio && !order_book_shadow.empty()) {
            req.action = usn::apps::OrderAction::Cancel;
            req.target_order_id = order_book_shadow.begin()->first;
        } else if (p <= cancel_ratio + replace_ratio && !order_book_shadow.empty()) {
            req.action = usn::apps::OrderAction::Replace;
            req.target_order_id = order_book_shadow.begin()->first;
            req.price += 1;
        }
        req.send_ts_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );

        const std::size_t inflight_bytes = outstanding_segments.size() * sizeof(usn::apps::OrderRequest);
        if (!usn::TcpStateMachine::can_send(conn, sizeof(req))) {
            ++send_blocked_total;
            continue;
        }
        if (!cc.can_send(inflight_bytes, sizeof(req), conn.peer_window)) {
            ++cwnd_blocked_total;
            continue;
        }

        const uint32_t seg_seq = conn.send_seq;
        uint8_t* buf = pool.allocate();
        usn::Packet packet = usn::TcpProtocol::create_data(
            conn,
            reinterpret_cast<const uint8_t*>(&req),
            sizeof(req),
            buf
        );
        std::vector<uint8_t> wire_copy(packet.data, packet.data + packet.len);
        ssize_t n = usn::TcpSocket::write_to(fd, packet.data, packet.len);
        pool.deallocate(packet.data);
        if (n != static_cast<ssize_t>(packet.len)) {
            if (n < 0) {
                std::perror("write");
            } else {
                std::cerr << "[order_client] partial write\n";
            }
            g_running.store(false, std::memory_order_release);
            break;
        }
        ++req_sent_total;
        tracer.emit(
            "order_client",
            "tcp_tx_order",
            req.client_order_id,
            req.send_ts_ns,
            outstanding_segments.size(),
            0
        );
        last_activity = std::chrono::steady_clock::now();

        const auto now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        retrans_q.track_segment(seg_seq, sizeof(req), now_ms, rto_estimator.rto_ms());
        outstanding_segments[seg_seq] = std::move(wire_copy);
        conn.send_seq += static_cast<uint32_t>(sizeof(req));
        auto send_ts = std::chrono::steady_clock::now();

        std::array<uint8_t, 256> ack_buf{};
        ssize_t nread = -1;
        while (g_running.load(std::memory_order_acquire)) {
            nread = usn::TcpSocket::read_from(fd, ack_buf.data(), ack_buf.size());
            if (nread > 0) {
                break;
            }
            if (nread == 0) {
                std::cerr << "[order_client] gateway closed while waiting ack\n";
                g_running.store(false, std::memory_order_release);
                break;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                ++ack_timeout_total;
                std::perror("read ack");
                g_running.store(false, std::memory_order_release);
                break;
            }

            const auto tick_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count()
            );
            const auto due = retrans_q.collect_due(tick_ms, rto_estimator.rto_ms(), 5);
            for (auto it = outstanding_segments.begin(); it != outstanding_segments.end();) {
                if (!retrans_q.contains(it->first)) {
                    ++retransmit_drop_total;
                    it = outstanding_segments.erase(it);
                } else {
                    ++it;
                }
            }
            if (due.empty()) {
                continue;
            }
            cc.on_loss();
            ++congestion_loss_total;
            for (const auto& entry : due) {
                auto it_out = outstanding_segments.find(entry.seq_start);
                if (it_out == outstanding_segments.end()) {
                    continue;
                }
                const auto& seg = it_out->second;
                ssize_t rw = usn::TcpSocket::write_to(fd, seg.data(), seg.size());
                if (rw == static_cast<ssize_t>(seg.size())) {
                    ++retransmit_total;
                } else {
                    ++retransmit_failed_total;
                }
            }
        }
        if (!g_running.load(std::memory_order_acquire) || nread <= 0) {
            break;
        }

        usn::Packet ack_packet(ack_buf.data(), static_cast<std::size_t>(nread));
        usn::TcpHeader ack_header{};
        const uint8_t* ack_payload = nullptr;
        std::size_t ack_payload_len = 0;
        if (!usn::TcpProtocol::parse(ack_packet, ack_header, ack_payload, ack_payload_len) ||
            usn::TcpProtocol::is_malformed_header(ack_header)) {
            std::cerr << "[order_client] invalid tcp ack frame\n";
            g_running.store(false, std::memory_order_release);
            break;
        }
        if (!ack_header.has_ack()) {
            std::cerr << "[order_client] non-ack frame received\n";
            g_running.store(false, std::memory_order_release);
            break;
        }
        usn::TcpStateMachine::on_window_update(conn, ack_header.window);
        conn.recv_ack = ack_header.ack_num;
        const std::size_t acked_segments = retrans_q.ack_up_to(ack_header.ack_num);
        cc.on_ack(acked_segments * sizeof(usn::apps::OrderRequest));
        for (auto it = outstanding_segments.begin(); it != outstanding_segments.end();) {
            const uint32_t end_seq = it->first + static_cast<uint32_t>(sizeof(usn::apps::OrderRequest));
            if (end_seq <= ack_header.ack_num) {
                it = outstanding_segments.erase(it);
            } else {
                ++it;
            }
        }
        if (ack_payload_len >= sizeof(usn::apps::OrderAck)) {
            usn::apps::OrderAck ack{};
            std::memcpy(&ack, ack_payload, sizeof(ack));
            if (ack.client_order_id != req.client_order_id) {
                std::cerr << "[order_client] ack client_order_id mismatch req=" << req.client_order_id
                          << " ack=" << ack.client_order_id << "\n";
            }
        }

        auto rtt_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - send_ts
        ).count();
        rto_estimator.sample_rtt(static_cast<uint64_t>(std::max<int64_t>(1, rtt_us / 1000)));
        rtt_us_samples.push_back(static_cast<uint64_t>(rtt_us));
        ++ack_recv_total;
        tracer.emit(
            "order_client",
            "tcp_rx_ack",
            req.client_order_id,
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count()
            ),
            outstanding_segments.size(),
            static_cast<uint64_t>(rtt_us)
        );
        last_activity = std::chrono::steady_clock::now();
        if (req.action == usn::apps::OrderAction::New) {
            ++new_total;
            rtt_new.push_back(static_cast<uint64_t>(rtt_us));
            order_book_shadow[req.client_order_id] = req;
        } else if (req.action == usn::apps::OrderAction::Cancel) {
            ++cancel_total;
            rtt_cancel.push_back(static_cast<uint64_t>(rtt_us));
            order_book_shadow.erase(req.target_order_id);
        } else {
            ++replace_total;
            rtt_replace.push_back(static_cast<uint64_t>(rtt_us));
            auto it = order_book_shadow.find(req.target_order_id);
            if (it != order_book_shadow.end()) {
                it->second.price = req.price;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds(1)) {
            std::cout << "[order_client][metrics] req_sent_total=" << req_sent_total
                      << " ack_recv_total=" << ack_recv_total
                      << " ack_timeout_total=" << ack_timeout_total
                      << " reconnect_total=" << reconnect_total
                      << " send_blocked_total=" << send_blocked_total
                      << " retransmit_total=" << retransmit_total
                      << " retransmit_failed_total=" << retransmit_failed_total
                      << " retransmit_drop_total=" << retransmit_drop_total
                      << " cwnd_blocked_total=" << cwnd_blocked_total
                      << " congestion_loss_total=" << congestion_loss_total
                      << " cwnd_bytes=" << cc.cwnd_bytes()
                      << " ssthresh_bytes=" << cc.ssthresh_bytes()
                      << " rto_ms=" << rto_estimator.rto_ms()
                      << " keepalive_sent_total=" << keepalive_sent_total
                      << " new_total=" << new_total
                      << " cancel_total=" << cancel_total
                      << " replace_total=" << replace_total
                      << " rtt_p50_us=" << percentile_us(rtt_us_samples, 0.50)
                      << " rtt_p99_us=" << percentile_us(rtt_us_samples, 0.99)
                      << " rtt_p999_us=" << percentile_us(rtt_us_samples, 0.999)
                      << " rtt_new_p99_us=" << percentile_us(rtt_new, 0.99)
                      << " rtt_cancel_p99_us=" << percentile_us(rtt_cancel, 0.99)
                      << " rtt_replace_p99_us=" << percentile_us(rtt_replace, 0.99) << "\n";
            last_report = now;
        }
        }

        std::this_thread::sleep_for(burst_interval);
    }

    std::cout << "[order_client][final] req_sent_total=" << req_sent_total
              << " ack_recv_total=" << ack_recv_total
              << " ack_timeout_total=" << ack_timeout_total
              << " reconnect_total=" << reconnect_total
              << " send_blocked_total=" << send_blocked_total
              << " retransmit_total=" << retransmit_total
              << " retransmit_failed_total=" << retransmit_failed_total
              << " retransmit_drop_total=" << retransmit_drop_total
              << " cwnd_blocked_total=" << cwnd_blocked_total
              << " congestion_loss_total=" << congestion_loss_total
              << " cwnd_bytes=" << cc.cwnd_bytes()
              << " ssthresh_bytes=" << cc.ssthresh_bytes()
              << " rto_ms=" << rto_estimator.rto_ms()
              << " keepalive_sent_total=" << keepalive_sent_total
              << " rtt_p50_us=" << percentile_us(rtt_us_samples, 0.50)
              << " rtt_p99_us=" << percentile_us(rtt_us_samples, 0.99)
              << " rtt_p999_us=" << percentile_us(rtt_us_samples, 0.999) << "\n";

    // Best-effort FIN/ACK teardown to exercise close path.
    if (fd >= 0 && state == ClientState::ACTIVE) {
        if (usn::TcpStateMachine::initiate_close(conn)) {
            uint8_t* fin_buf = pool.allocate();
            usn::Packet fin_pkt = usn::TcpProtocol::create_fin(conn, fin_buf);
            ssize_t fin_w = usn::TcpSocket::write_to(fd, fin_pkt.data, fin_pkt.len);
            if (fin_w < 0) {
                std::perror("write fin");
            }
            pool.deallocate(fin_pkt.data);
            conn.send_seq += 1;

            std::array<uint8_t, 256> close_buf{};
            for (int i = 0; i < 4; ++i) {
                ssize_t nread = usn::TcpSocket::read_from(fd, close_buf.data(), close_buf.size());
                if (nread <= 0) {
                    break;
                }
                usn::Packet close_pkt(close_buf.data(), static_cast<std::size_t>(nread));
                usn::TcpHeader close_h{};
                const uint8_t* close_payload = nullptr;
                std::size_t close_payload_len = 0;
                if (!usn::TcpProtocol::parse(close_pkt, close_h, close_payload, close_payload_len) ||
                    usn::TcpProtocol::is_malformed_header(close_h)) {
                    continue;
                }
                (void)usn::TcpStateMachine::handle_incoming(conn, close_h, close_payload_len);
                if (close_h.has_fin()) {
                    uint8_t* ack_buf = pool.allocate();
                    usn::Packet ack_pkt = usn::TcpProtocol::create_ack(conn, conn.send_ack, ack_buf);
                    ssize_t ack_w = usn::TcpSocket::write_to(fd, ack_pkt.data, ack_pkt.len);
                    if (ack_w < 0) {
                        std::perror("write close-ack");
                    }
                    pool.deallocate(ack_pkt.data);
                }
                if (conn.state == usn::TcpState::TIME_WAIT || conn.state == usn::TcpState::CLOSED) {
                    break;
                }
            }
        }
    }

    state = ClientState::STOPPED;
    std::cout << "[order_client][state] " << state_name(state) << "\n";

    ::close(fd);
    return 0;
}

