// SPDX-License-Identifier: MIT
//
// UDP 行情发布端骨架（组播）：
// - 周期性向组播地址发送带 seq 的 MarketDataIncrement

#include <usn/apps/messages.hpp>

#include <usn/core/memory_pool.hpp>
#include <usn/optimization/numa_utils.hpp>
#include <usn/protocol/udp_protocol.hpp>
#include <usn/protocol/udp_socket.hpp>

#include <arpa/inet.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

// 典型行情组播网段示例（本地环境需要配置路由/组播支持）
constexpr const char* kMulticastGroup = "239.0.0.1";
constexpr int kMulticastPort = 9100;
constexpr uint16_t kSrcPort = 9001;
constexpr uint32_t kPublisherIp = 0x7F000001u;  // 127.0.0.1 (network order)
constexpr uint32_t kGroupIp = 0xEF000001u;      // 239.0.0.1 (network order)

}  // namespace

int main(int argc, char** argv) {
    usn::CpuAffinity::bind_to_cpu(0);
    int rate = 2;
    int payload_size = 64;
    int streams = 1;
    int jitter_ms = 0;
    double drop_rate = 0.0;
    double reorder_rate = 0.0;
    uint32_t seed = 42;

    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--rate") {
            const int val = std::atoi(argv[i + 1]);
            rate = std::max(1, val);
        } else if (key == "--payload-size") {
            const int val = std::atoi(argv[i + 1]);
            payload_size = std::max(static_cast<int>(sizeof(usn::apps::MarketDataIncrement)), val);
        } else if (key == "--streams") {
            const int val = std::atoi(argv[i + 1]);
            streams = std::max(1, val);
        } else if (key == "--jitter-ms") {
            const int val = std::atoi(argv[i + 1]);
            jitter_ms = std::max(0, val);
        } else if (key == "--drop-rate") {
            drop_rate = std::clamp(std::atof(argv[i + 1]), 0.0, 1.0);
        } else if (key == "--reorder-rate") {
            reorder_rate = std::clamp(std::atof(argv[i + 1]), 0.0, 1.0);
        } else if (key == "--seed") {
            const int val = std::atoi(argv[i + 1]);
            seed = static_cast<uint32_t>(std::max(1, val));
        }
    }

    usn::UdpSocket socket;
    if (!socket.create()) {
        std::perror("socket");
        return 1;
    }

    int ttl = 1;
    ::setsockopt(socket.fd(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // 预分配内存池：每块足够容纳 UDP header(8) + MarketDataIncrement
    const std::size_t payload_bytes = static_cast<std::size_t>(
        std::max(payload_size, static_cast<int>(sizeof(usn::apps::MarketDataIncrement)))
    );
    const std::size_t kPacketBufSize = sizeof(usn::UdpHeader) + payload_bytes + 16;
    usn::MemoryPool pool(kPacketBufSize, 64);

    std::cout << "[feed_publisher] sending multicast to " << kMulticastGroup << ":" << kMulticastPort << "\n";

    uint64_t seq = 1;
    uint64_t sent_total = 0;
    auto last_report = std::chrono::steady_clock::now();
    const auto send_interval = std::chrono::microseconds(1000000 / std::max(1, rate));
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> bp_dist(1, 10000);
    std::uniform_int_distribution<int> jitter_dist(0, std::max(0, jitter_ms));
    std::vector<usn::apps::MarketDataIncrement> reorder_q;
    reorder_q.reserve(8);
    uint64_t dropped_total = 0;
    uint64_t reordered_total = 0;

    while (true) {
        usn::apps::MarketDataIncrement m{};
        m.seq          = seq++;
        m.stream_id    = static_cast<uint32_t>((m.seq - 1) % static_cast<uint64_t>(streams));
        m.instrument_id = 10001;
        m.bid_price    = 100000;
        m.bid_qty      = 10;
        m.ask_price    = 100010;
        m.ask_qty      = 10;
        m.send_ts_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );

        if (static_cast<double>(bp_dist(rng)) / 10000.0 <= drop_rate) {
            ++dropped_total;
        } else {
            reorder_q.push_back(m);
            if (static_cast<double>(bp_dist(rng)) / 10000.0 <= reorder_rate && reorder_q.size() >= 2) {
                std::swap(reorder_q[reorder_q.size() - 1], reorder_q[reorder_q.size() - 2]);
                ++reordered_total;
            }
            while (reorder_q.size() > 1) {
                const auto send_msg = reorder_q.front();
                reorder_q.erase(reorder_q.begin());

                std::vector<uint8_t> payload(payload_bytes, 0);
                std::memcpy(payload.data(), &send_msg, sizeof(send_msg));

                uint8_t* buf = pool.allocate();
                usn::Packet packet = usn::UdpProtocol::encapsulate(
                    payload.data(),
                    payload.size(),
                    kSrcPort,
                    static_cast<uint16_t>(kMulticastPort),
                    kPublisherIp,
                    kGroupIp,
                    true,
                    buf
                );

                ssize_t n = socket.sendto(packet.data, packet.len, kMulticastGroup, static_cast<uint16_t>(kMulticastPort));
                pool.deallocate(packet.data);

                if (n != static_cast<ssize_t>(packet.len)) {
                    if (n < 0) {
                        std::perror("sendto");
                    } else {
                        std::cerr << "[feed_publisher] partial send\n";
                    }
                    socket.close();
                    return 1;
                }
                ++sent_total;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds(1)) {
            std::cout << "[feed_publisher][metrics] sent_total=" << sent_total
                      << " rate=" << rate
                      << " payload_size=" << payload_bytes
                      << " streams=" << streams
                      << " dropped_total=" << dropped_total
                      << " reordered_total=" << reordered_total
                      << " drop_rate=" << drop_rate
                      << " reorder_rate=" << reorder_rate << "\n";
            last_report = now;
        }
        if (jitter_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(jitter_dist(rng)));
        }
        std::this_thread::sleep_for(send_interval);
    }

    socket.close();
    return 0;
}

