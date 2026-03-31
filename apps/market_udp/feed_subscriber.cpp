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
#include <iostream>

namespace {

constexpr const char* kMulticastGroup = "239.0.0.1";
constexpr int         kMulticastPort  = 9100;

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

int main() {
    usn::CpuAffinity::bind_to_cpu(1);

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

    // 处理单个数据包的逻辑
    auto process_packet = [&](usn::Packet& pkt) {
        usn::UdpHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payload_len = 0;
        if (!usn::UdpProtocol::parse(pkt, header, payload, payload_len)) {
            std::cerr << "[feed_subscriber] invalid udp packet\n";
            return;
        }
        if (payload_len != sizeof(usn::apps::MarketDataIncrement)) {
            std::cerr << "[feed_subscriber] unexpected payload size=" << payload_len << "\n";
            return;
        }

        usn::apps::MarketDataIncrement m{};
        std::memcpy(&m, payload, sizeof(m));

        ++total;
        if (m.seq != expected_seq) {
            std::cout << "[feed_subscriber] GAP: expected=" << expected_seq << " got=" << m.seq << "\n";
            if (m.seq > expected_seq) {
                gaps += (m.seq - expected_seq);
            }
            expected_seq = m.seq + 1;
        } else {
            ++expected_seq;
        }

        if (total % 10 == 0) {
            std::cout << "[feed_subscriber] stats: total=" << total << " gaps=" << gaps << "\n";
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

    ::close(fd);
    return 0;
}
