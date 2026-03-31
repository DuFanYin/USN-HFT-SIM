// SPDX-License-Identifier: MIT
//
// Epoll 基础测试

#include <usn/io/epoll_wrapper.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
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

TEST(Epoll, WaitWithTimeout) {
    EpollWrapper epoll;
    std::vector<struct epoll_event> events;
    EpollWaitOptions options;
    options.timeout_ms = 1;

    const auto result = epoll.wait_with_options(events, options);
    EXPECT_EQ(result.count, 0);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(result.status, EpollWaitStatus::Timeout);
}

TEST(Epoll, WaitCancelledBeforeCall) {
    EpollWrapper epoll;
    std::vector<struct epoll_event> events;
    std::atomic<bool> cancelled{true};
    EpollWaitOptions options;
    options.cancel_flag = &cancelled;
    options.timeout_ms = -1;

    const auto result = epoll.wait_with_options(events, options);
    EXPECT_EQ(result.count, 0);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(result.status, EpollWaitStatus::Cancelled);
}

TEST(Epoll, WaitCancelledDuringLoop) {
    EpollWrapper epoll;
    std::vector<struct epoll_event> events;
    std::atomic<bool> cancelled{false};

    std::thread canceller([&cancelled]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        cancelled.store(true, std::memory_order_release);
    });

    EpollWaitOptions options;
    options.cancel_flag = &cancelled;
    options.timeout_ms = -1;
    options.poll_interval_ms = 1;

    const auto result = epoll.wait_with_options(events, options);
    canceller.join();

    EXPECT_EQ(result.count, 0);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(result.status, EpollWaitStatus::Cancelled);
}

TEST(Epoll, InvalidMaxEventsReturnsSysError) {
    EpollWrapper epoll(0);
    std::vector<struct epoll_event> events;
    EpollWaitOptions options;
    options.timeout_ms = 0;

    const auto result = epoll.wait_with_options(events, options);
    EXPECT_EQ(result.status, EpollWaitStatus::SysError);
    EXPECT_NE(result.error, 0);
}
