// SPDX-License-Identifier: MIT
//
// UDP 行情发布端骨架（组播）：
// - 周期性向组播地址发送带 seq 的 MarketDataIncrement

#include "../common/messages.hpp"

#include <usn/core/memory_pool.hpp>
#include <usn/optimization/numa_utils.hpp>
#include <usn/protocol/udp_protocol.hpp>
#include <usn/protocol/udp_socket.hpp>

#include <arpa/inet.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace {

// 典型行情组播网段示例（本地环境需要配置路由/组播支持）
constexpr const char* kMulticastGroup = "239.0.0.1";
constexpr int kMulticastPort = 9100;
constexpr uint16_t kSrcPort = 9001;
constexpr uint32_t kPublisherIp = 0x7F000001u;  // 127.0.0.1 (network order)
constexpr uint32_t kGroupIp = 0xEF000001u;      // 239.0.0.1 (network order)

}  // namespace

int main() {
    usn::CpuAffinity::bind_to_cpu(0);

    usn::UdpSocket socket;
    if (!socket.create()) {
        std::perror("socket");
        return 1;
    }

    int ttl = 1;
    ::setsockopt(socket.fd(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // 预分配内存池：每块足够容纳 UDP header(8) + MarketDataIncrement
    constexpr std::size_t kPacketBufSize = sizeof(usn::UdpHeader) + sizeof(usn::apps::MarketDataIncrement) + 16;
    usn::MemoryPool pool(kPacketBufSize, 64);

    std::cout << "[feed_publisher] sending multicast to " << kMulticastGroup << ":" << kMulticastPort << "\n";

    uint64_t seq = 1;
    while (true) {
        usn::apps::MarketDataIncrement m{};
        m.seq          = seq++;
        m.instrument_id = 10001;
        m.bid_price    = 100000;
        m.bid_qty      = 10;
        m.ask_price    = 100010;
        m.ask_qty      = 10;

        uint8_t* buf = pool.allocate();
        usn::Packet packet = usn::UdpProtocol::encapsulate(
            reinterpret_cast<const uint8_t*>(&m),
            sizeof(m),
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
            break;
        }

        std::cout << "[feed_publisher] sent seq=" << m.seq << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    socket.close();
    return 0;
}

