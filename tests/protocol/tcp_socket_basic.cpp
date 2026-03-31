// SPDX-License-Identifier: MIT
//
// TCP socket wrapper basic tests

#include <usn/protocol/tcp_socket.hpp>
#include <gtest/gtest.h>

using namespace usn;

TEST(TcpSocket, ConnectInvalidAddressFails) {
    auto s = TcpSocket::connect_to("256.1.1.1", 9000, 100);
    EXPECT_FALSE(s.is_open());
}

TEST(TcpSocket, CreateServerAndClose) {
    auto s = TcpSocket::create_server(0, 8, true);
    ASSERT_TRUE(s.is_open());
    EXPECT_GE(s.fd(), 0);
    s.close();
    EXPECT_FALSE(s.is_open());
}
