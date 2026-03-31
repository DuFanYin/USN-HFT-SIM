// SPDX-License-Identifier: MIT
//
// Epoll 基础测试

#include <usn/io/epoll_wrapper.hpp>
#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/socket.h>

using namespace usn;

TEST(Epoll, Create) {
    EpollWrapper epoll;
    EXPECT_GE(epoll.fd(), 0);
}

TEST(Epoll, AddRemove) {
    EpollWrapper epoll;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);

    EXPECT_TRUE(epoll.add(sock, EPOLLIN | EPOLLET));
    EXPECT_TRUE(epoll.remove(sock));

    close(sock);
}
