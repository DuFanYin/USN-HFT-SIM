// SPDX-License-Identifier: MIT
//
// TCP 订单客户端骨架：
// - 连接到本地订单网关
// - 周期性发送简单的 OrderRequest

#include "../common/messages.hpp"

#include <usn/core/memory_pool.hpp>
#include <usn/optimization/numa_utils.hpp>
#include <usn/protocol/tcp_protocol.hpp>

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
#include <vector>

namespace {

constexpr const char* kGatewayIp   = "127.0.0.1";
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

int connect_to_gateway(int timeout_ms) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(kGatewayPort);
    if (::inet_pton(AF_INET, kGatewayIp, &addr.sin_addr) <= 0) {
        std::perror("inet_pton");
        ::close(fd);
        return -1;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("connect");
        ::close(fd);
        return -1;
    }

    // 防止 ACK 等待无限阻塞
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return fd;
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
        }
    }

    ClientState state = ClientState::CONNECTING;
    std::cout << "[order_client][state] " << state_name(state) << "\n";
    int fd = connect_to_gateway(timeout_ms);
    if (fd < 0) {
        state = ClientState::STOPPED;
        std::cout << "[order_client][state] " << state_name(state) << "\n";
        return 1;
    }
    state = ClientState::ACTIVE;
    std::cout << "[order_client][state] " << state_name(state) << "\n";

    std::cout << "[order_client] connected to " << kGatewayIp << ":" << kGatewayPort << "\n";

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

    uint64_t order_id = 1;
    uint64_t req_sent_total = 0;
    uint64_t ack_recv_total = 0;
    uint64_t ack_timeout_total = 0;
    uint64_t reconnect_total = 0;
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
    auto last_report = std::chrono::steady_clock::now();
    const auto burst_interval = std::chrono::microseconds((1000000 * burst) / std::max(1, qps));
    auto last_disconnect = std::chrono::steady_clock::now();

    while (g_running.load(std::memory_order_acquire)) {
        if (disconnect_interval_s > 0 &&
            std::chrono::steady_clock::now() - last_disconnect >= std::chrono::seconds(disconnect_interval_s)) {
            ::close(fd);
            state = ClientState::RETRYING;
            std::cout << "[order_client][state] " << state_name(state) << "\n";
            fd = connect_to_gateway(timeout_ms);
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            ++reconnect_total;
            state = ClientState::ACTIVE;
            std::cout << "[order_client][state] " << state_name(state) << "\n";
            last_disconnect = std::chrono::steady_clock::now();
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

        uint8_t* buf = pool.allocate();
        usn::Packet packet = usn::TcpProtocol::create_data(
            conn,
            reinterpret_cast<const uint8_t*>(&req),
            sizeof(req),
            buf
        );
        ssize_t n = ::write(fd, packet.data, packet.len);
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

        conn.send_seq += static_cast<uint32_t>(sizeof(req));
        auto send_ts = std::chrono::steady_clock::now();

        std::array<uint8_t, 256> ack_buf{};
        ssize_t nread = ::read(fd, ack_buf.data(), ack_buf.size());
        if (nread <= 0) {
            if (nread < 0) {
                ++ack_timeout_total;
                std::perror("read ack");
            } else {
                std::cerr << "[order_client] gateway closed while waiting ack\n";
            }
            g_running.store(false, std::memory_order_release);
            break;
        }

        usn::Packet ack_packet(ack_buf.data(), static_cast<std::size_t>(nread));
        usn::TcpHeader ack_header{};
        const uint8_t* ack_payload = nullptr;
        std::size_t ack_payload_len = 0;
        if (!usn::TcpProtocol::parse(ack_packet, ack_header, ack_payload, ack_payload_len)) {
            std::cerr << "[order_client] invalid tcp ack frame\n";
            g_running.store(false, std::memory_order_release);
            break;
        }
        if (!ack_header.has_ack()) {
            std::cerr << "[order_client] non-ack frame received\n";
            g_running.store(false, std::memory_order_release);
            break;
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
        rtt_us_samples.push_back(static_cast<uint64_t>(rtt_us));
        ++ack_recv_total;
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
              << " rtt_p50_us=" << percentile_us(rtt_us_samples, 0.50)
              << " rtt_p99_us=" << percentile_us(rtt_us_samples, 0.99)
              << " rtt_p999_us=" << percentile_us(rtt_us_samples, 0.999) << "\n";
    state = ClientState::STOPPED;
    std::cout << "[order_client][state] " << state_name(state) << "\n";

    ::close(fd);
    return 0;
}

