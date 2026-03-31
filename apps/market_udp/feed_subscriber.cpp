// SPDX-License-Identifier: MIT
//
// UDP 行情订阅端骨架（组播）：
// - 加入组播组
// - 接收 MarketDataIncrement，做简单的 seq 连续性统计

#include "../common/messages.hpp"

#include <usn/protocol/udp_protocol.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
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

    return fd;
}

}  // namespace

int main() {
    int fd = create_and_join_multicast();
    if (fd < 0) {
        return 1;
    }

    std::cout << "[feed_subscriber] listening on multicast " << kMulticastGroup << ":" << kMulticastPort << "\n";

    uint64_t expected_seq = 1;
    uint64_t total        = 0;
    uint64_t gaps         = 0;

    while (true) {
        std::array<uint8_t, 2048> raw{};
        ssize_t n = ::recv(fd, raw.data(), raw.size(), 0);
        if (n < 0) {
            std::perror("recv");
            break;
        }

        usn::Packet udp_packet(raw.data(), static_cast<std::size_t>(n));
        usn::UdpHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payload_len = 0;
        if (!usn::UdpProtocol::parse(udp_packet, header, payload, payload_len)) {
            std::cerr << "[feed_subscriber] invalid udp packet\n";
            continue;
        }
        if (payload_len != sizeof(usn::apps::MarketDataIncrement)) {
            std::cerr << "[feed_subscriber] unexpected payload size=" << payload_len << "\n";
            continue;
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
    }

    ::close(fd);
    return 0;
}

