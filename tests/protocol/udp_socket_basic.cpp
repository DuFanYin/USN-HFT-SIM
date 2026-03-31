// SPDX-License-Identifier: MIT
//
// UDP socket wrapper basic tests

#include <usn/protocol/udp_socket.hpp>
#include <gtest/gtest.h>

using namespace usn;

TEST(UdpSocket, UnifiedRecvInvalidFd) {
    UdpSocket sock;
    PacketRing ring(8);
    auto r = sock.recv_batch_unified(ring, 4);
    EXPECT_EQ(r.status, UnifiedIOStatus::SysError);
    EXPECT_NE(r.error, 0);
}

TEST(UdpSocket, UnifiedSendInvalidFd) {
    UdpSocket sock;
    Packet packets[1];
    auto r = sock.send_batch_unified(std::span<const Packet>(packets, 1));
    EXPECT_EQ(r.status, UnifiedIOStatus::SysError);
    EXPECT_NE(r.error, 0);
}
