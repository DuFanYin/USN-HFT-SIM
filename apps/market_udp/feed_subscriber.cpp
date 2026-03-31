// SPDX-License-Identifier: MIT
//
// UDP 行情订阅端骨架（组播）：
// - 加入组播组
// - 使用 BatchRecv + PacketRing 批量接收
// - 通过 BusyPoller 低延迟轮询
// - 接收 MarketDataIncrement，做简单的 seq 连续性统计

#include "../common/messages.hpp"

#include <usn/core/packet_ring.hpp>
#include <usn/io/batch_io.hpp>
#include <usn/io/busy_poll.hpp>
#include <usn/optimization/numa_utils.hpp>
#include <usn/protocol/udp_protocol.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kMulticastGroup = "239.0.0.1";
constexpr int         kMulticastPort  = 9100;
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

int create_and_join_multicast() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }

    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(kMulticastPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        std::perror("bind");
        ::close(fd);
        return -1;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = ::inet_addr(kMulticastGroup);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::perror("IP_ADD_MEMBERSHIP");
        ::close(fd);
        return -1;
    }

    // 设为非阻塞，供 BusyPoller / BatchRecv 使用
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

}  // namespace

int main(int argc, char** argv) {
    usn::CpuAffinity::bind_to_cpu(1);
    int subscriber_id = 0;
    int artificial_delay_ms = 0;
    std::string summary_file;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--subscriber-id") {
            const int val = std::atoi(argv[i + 1]);
            subscriber_id = std::max(0, val);
        } else if (key == "--artificial-delay-ms") {
            const int val = std::atoi(argv[i + 1]);
            artificial_delay_ms = std::max(0, val);
        } else if (key == "--summary-file") {
            summary_file = argv[i + 1];
        }
    }

    ::signal(SIGINT, handle_signal);
    ::signal(SIGTERM, handle_signal);

    int fd = create_and_join_multicast();
    if (fd < 0) {
        return 1;
    }

    std::cout << "[feed_subscriber] listening on multicast " << kMulticastGroup << ":" << kMulticastPort << "\n";

    // 批量接收 + 环形缓冲区
    usn::BatchRecv batch_recv(fd);
    usn::PacketRing ring(64);

    uint64_t expected_seq = 1;
    uint64_t total        = 0;
    uint64_t gaps         = 0;
    uint64_t reorder_events = 0;
    uint64_t max_reorder_depth = 0;
    std::vector<uint64_t> latency_us_samples;
    latency_us_samples.reserve(1 << 14);
    auto last_report = std::chrono::steady_clock::now();

    // 处理单个数据包的逻辑
    auto process_packet = [&](usn::Packet& pkt) {
        usn::UdpHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payload_len = 0;
        if (!usn::UdpProtocol::parse(pkt, header, payload, payload_len)) {
            std::cerr << "[feed_subscriber] invalid udp packet\n";
            return;
        }
        if (payload_len < sizeof(usn::apps::MarketDataIncrement)) {
            std::cerr << "[feed_subscriber] unexpected payload size=" << payload_len << "\n";
            return;
        }

        usn::apps::MarketDataIncrement m{};
        std::memcpy(&m, payload, sizeof(m));

        ++total;
        if (m.seq != expected_seq) {
            if (m.seq > expected_seq) {
                gaps += (m.seq - expected_seq);
                expected_seq = m.seq + 1;
            } else {
                ++reorder_events;
                max_reorder_depth = std::max(max_reorder_depth, expected_seq - m.seq);
            }
        } else {
            ++expected_seq;
        }

        if (artificial_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(artificial_delay_ms));
        }

        const uint64_t now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        if (now_ns >= m.send_ts_ns) {
            latency_us_samples.push_back((now_ns - m.send_ts_ns) / 1000u);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds(1)) {
            std::cout << "[feed_subscriber][metrics] id=" << subscriber_id
                      << " recv_total=" << total
                      << " gaps=" << gaps
                      << " reorder_events=" << reorder_events
                      << " max_reorder_depth=" << max_reorder_depth
                      << " latency_p50_us=" << percentile_us(latency_us_samples, 0.50)
                      << " latency_p99_us=" << percentile_us(latency_us_samples, 0.99)
                      << " latency_p999_us=" << percentile_us(latency_us_samples, 0.999)
                      << " latency_max_us=" << percentile_us(latency_us_samples, 1.00) << "\n";
            last_report = now;
        }
    };

    // 使用 BusyPoller 驱动接收循环
    usn::BusyPollConfig poll_cfg;
    poll_cfg.poll_interval = std::chrono::nanoseconds(1000);
    poll_cfg.max_idle_time = std::chrono::nanoseconds(100000);  // 100 us
    poll_cfg.adaptive = true;

    usn::BusyPoller poller(poll_cfg);

    poller.start(
        [&]() -> bool {
            if (!g_running.load(std::memory_order_acquire)) {
                poller.stop();
                return false;
            }
            // 批量接收到 ring
            auto result = batch_recv.recv_batch(ring, 16);

            // 从 ring 取出并处理
            usn::Packet pkt;
            bool got_data = false;
            while (ring.try_pop(pkt)) {
                process_packet(pkt);
                got_data = true;
            }
            return got_data || result.count > 0;
        },
        [&]() {
            // 空闲回调：自适应调整轮询间隔
            poller.adaptive_adjust();
        }
    );

    std::cout << "[feed_subscriber][final] id=" << subscriber_id
              << " recv_total=" << total
              << " gaps=" << gaps
              << " reorder_events=" << reorder_events
              << " max_reorder_depth=" << max_reorder_depth
              << " latency_p50_us=" << percentile_us(latency_us_samples, 0.50)
              << " latency_p99_us=" << percentile_us(latency_us_samples, 0.99)
              << " latency_p999_us=" << percentile_us(latency_us_samples, 0.999)
              << " latency_max_us=" << percentile_us(latency_us_samples, 1.00) << "\n";
    if (!summary_file.empty()) {
        std::ofstream out(summary_file, std::ios::trunc);
        if (out.good()) {
            out << "id=" << subscriber_id
                << " recv_total=" << total
                << " gaps=" << gaps
                << " reorder_events=" << reorder_events
                << " max_reorder_depth=" << max_reorder_depth
                << " latency_p50_us=" << percentile_us(latency_us_samples, 0.50)
                << " latency_p99_us=" << percentile_us(latency_us_samples, 0.99)
                << " latency_p999_us=" << percentile_us(latency_us_samples, 0.999)
                << " latency_max_us=" << percentile_us(latency_us_samples, 1.00) << "\n";
        }
    }

    ::close(fd);
    return 0;
}
