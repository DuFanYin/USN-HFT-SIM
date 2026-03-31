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
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

constexpr const char* kGatewayIp   = "127.0.0.1";
constexpr int         kGatewayPort = 9000;
constexpr uint16_t    kClientPort  = 9101;

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

    // 防止 ACK 等待无限阻塞
    timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return fd;
}

}  // namespace

int main() {
    usn::CpuAffinity::bind_to_cpu(1);

    int fd = connect_to_gateway();
    if (fd < 0) {
        return 1;
    }

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
    while (true) {
        usn::apps::OrderRequest req{};
        req.client_order_id = order_id++;
        req.instrument_id   = 10001;
        req.side            = (order_id % 2 == 0) ? usn::apps::Side::Buy : usn::apps::Side::Sell;
        req.price           = 100000 + static_cast<uint32_t>(order_id % 100);  // 1000.00x
        req.quantity        = 1;

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
            break;
        }

        conn.send_seq += static_cast<uint32_t>(sizeof(req));
        auto send_ts = std::chrono::steady_clock::now();

        std::array<uint8_t, 256> ack_buf{};
        ssize_t nread = ::read(fd, ack_buf.data(), ack_buf.size());
        if (nread <= 0) {
            if (nread < 0) {
                std::perror("read ack");
            } else {
                std::cerr << "[order_client] gateway closed while waiting ack\n";
            }
            break;
        }

        usn::Packet ack_packet(ack_buf.data(), static_cast<std::size_t>(nread));
        usn::TcpHeader ack_header{};
        const uint8_t* ack_payload = nullptr;
        std::size_t ack_payload_len = 0;
        if (!usn::TcpProtocol::parse(ack_packet, ack_header, ack_payload, ack_payload_len)) {
            std::cerr << "[order_client] invalid tcp ack frame\n";
            break;
        }
        if (!ack_header.has_ack()) {
            std::cerr << "[order_client] non-ack frame received\n";
            break;
        }

        auto rtt_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - send_ts
        ).count();

        std::cout << "[order_client] sent order client_order_id=" << req.client_order_id
                  << ", ack_num=" << ack_header.ack_num
                  << ", ack_payload=" << ack_payload_len
                  << "B, rtt_us=" << rtt_us << "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    ::close(fd);
    return 0;
}

