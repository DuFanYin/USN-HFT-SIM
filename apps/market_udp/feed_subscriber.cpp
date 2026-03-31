// SPDX-License-Identifier: MIT
//
// UDP 行情订阅端骨架（组播）：
// - 加入组播组
// - 接收 MarketDataIncrement，做简单的 seq 连续性统计

#include "../common/messages.hpp"

#include <arpa/inet.h>
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
        usn::apps::MarketDataIncrement m{};
        ssize_t                        n = ::recv(fd, &m, sizeof(m), 0);
        if (n < 0) {
            std::perror("recv");
            break;
        }
        if (n != static_cast<ssize_t>(sizeof(m))) {
            std::cerr << "[feed_subscriber] partial recv\n";
            continue;
        }

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

