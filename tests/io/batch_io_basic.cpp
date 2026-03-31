// SPDX-License-Identifier: MIT
//
// Batch I/O 基础功能测试

#include <usn/io/batch_io.hpp>
#include <usn/core/packet_ring.hpp>
#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>

using namespace usn;

TEST(BatchIO, RecvInterfaceNoData) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        GTEST_SKIP() << "Cannot create socket";
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    BatchRecv recv(sock);
    PacketRing ring(1024);

    auto result = recv.recv_batch(ring, 64);
    EXPECT_TRUE(result.success());
    EXPECT_EQ(result.count, 0u);

    close(sock);
}

TEST(BatchIO, SendInterfaceWorks) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        GTEST_SKIP() << "Cannot create socket";
    }

    BatchSend send(sock);

    std::vector<uint8_t> data1(100, 0xAA);
    std::vector<uint8_t> data2(200, 0xBB);

    Packet packets[] = {
        Packet(data1.data(), data1.size()),
        Packet(data2.data(), data2.size())
    };

    // 可能会失败（没有绑定地址），但接口不应崩溃
    send.send_batch(packets);

    close(sock);
}
