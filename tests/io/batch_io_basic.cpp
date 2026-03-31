// SPDX-License-Identifier: MIT
//
// Batch I/O 基础功能测试

#include <usn/io/batch_io.hpp>
#include <usn/core/packet_ring.hpp>
#include <gtest/gtest.h>
#include <atomic>
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
    EXPECT_EQ(result.status, BatchIOStatus::WouldBlock);

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

TEST(BatchIO, RecvTimeoutOptionReturnsTimeout) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        GTEST_SKIP() << "Cannot create socket";
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    BatchRecv recv(sock);
    PacketRing ring(64);
    BatchIOOptions options;
    options.timeout_ms = 1;

    auto result = recv.recv_batch(ring, 8, options);
    EXPECT_EQ(result.count, 0u);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(result.status, BatchIOStatus::Timeout);

    close(sock);
}

TEST(BatchIO, RecvCancelledBeforeCall) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        GTEST_SKIP() << "Cannot create socket";
    }

    BatchRecv recv(sock);
    PacketRing ring(64);
    std::atomic<bool> cancelled{true};
    BatchIOOptions options;
    options.cancel_flag = &cancelled;

    auto result = recv.recv_batch(ring, 8, options);
    EXPECT_EQ(result.count, 0u);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(result.status, BatchIOStatus::Cancelled);

    close(sock);
}

TEST(BatchIO, SendCancelledBeforeCall) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        GTEST_SKIP() << "Cannot create socket";
    }

    BatchSend send(sock);
    std::vector<uint8_t> data(32, 0x11);
    Packet packets[] = {Packet(data.data(), data.size())};
    std::atomic<bool> cancelled{true};
    BatchIOOptions options;
    options.cancel_flag = &cancelled;

    auto result = send.send_batch(packets, options);
    EXPECT_EQ(result.count, 0u);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(result.status, BatchIOStatus::Cancelled);

    close(sock);
}

TEST(BatchIO, InvalidSocketReturnsSysError) {
    BatchRecv recv(-1);
    PacketRing ring(64);
    auto recv_result = recv.recv_batch(ring, 8);
    EXPECT_EQ(recv_result.status, BatchIOStatus::SysError);
    EXPECT_NE(recv_result.error, 0);

    BatchSend send(-1);
    std::vector<uint8_t> data(32, 0x22);
    Packet packets[] = {Packet(data.data(), data.size())};
    auto send_result = send.send_batch(packets);
    EXPECT_EQ(send_result.status, BatchIOStatus::SysError);
    EXPECT_NE(send_result.error, 0);
}
