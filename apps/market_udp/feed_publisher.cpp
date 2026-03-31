// SPDX-License-Identifier: MIT
//
// UDP 行情发布端骨架（组播）：
// - 周期性向组播地址发送带 seq 的 MarketDataIncrement

#include <usn/apps/common/messages.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

// 典型行情组播网段示例（本地环境需要配置路由/组播支持）
constexpr const char* kMulticastGroup = "239.0.0.1";
constexpr int         kMulticastPort  = 9100;

int create_multicast_socket(sockaddr_in& dst_addr) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }

    int ttl = 1;
    ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    std::memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family      = AF_INET;
    dst_addr.sin_port        = htons(kMulticastPort);
    dst_addr.sin_addr.s_addr = ::inet_addr(kMulticastGroup);

    return fd;
}

}  // namespace

int main() {
    sockaddr_in dst{};
    int         fd = create_multicast_socket(dst);
    if (fd < 0) {
        return 1;
    }

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

        ssize_t n = ::sendto(fd, &m, sizeof(m), 0,
                             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
        if (n != static_cast<ssize_t>(sizeof(m))) {
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

    ::close(fd);
    return 0;
}

