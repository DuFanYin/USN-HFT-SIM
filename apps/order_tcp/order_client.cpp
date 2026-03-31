// SPDX-License-Identifier: MIT
//
// TCP 订单客户端骨架：
// - 连接到本地订单网关
// - 周期性发送简单的 OrderRequest

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

constexpr const char* kGatewayIp   = "127.0.0.1";
constexpr int         kGatewayPort = 9000;

int connect_to_gateway() {
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

    return fd;
}

}  // namespace

int main() {
    int fd = connect_to_gateway();
    if (fd < 0) {
        return 1;
    }

    std::cout << "[order_client] connected to " << kGatewayIp << ":" << kGatewayPort << "\n";

    uint64_t order_id = 1;
    while (true) {
        usn::apps::OrderRequest req{};
        req.client_order_id = order_id++;
        req.instrument_id   = 10001;
        req.side            = (order_id % 2 == 0) ? usn::apps::Side::Buy : usn::apps::Side::Sell;
        req.price           = 100000 + static_cast<uint32_t>(order_id % 100);  // 1000.00x
        req.quantity        = 1;

        ssize_t n = ::write(fd, &req, sizeof(req));
        if (n != static_cast<ssize_t>(sizeof(req))) {
            if (n < 0) {
                std::perror("write");
            } else {
                std::cerr << "[order_client] partial write\n";
            }
            break;
        }

        std::cout << "[order_client] sent order client_order_id=" << req.client_order_id << "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    ::close(fd);
    return 0;
}

