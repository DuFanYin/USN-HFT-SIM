// SPDX-License-Identifier: MIT
//
// Busy Poll 基础测试

#include <usn/io/busy_poll.hpp>
#include <gtest/gtest.h>
#include <atomic>

using namespace usn;

TEST(BusyPoll, CancelledBeforeStart) {
    BusyPoller poller;
    std::atomic<bool> cancelled{true};
    BusyPollControl control;
    control.cancel_flag = &cancelled;

    const auto result = poller.start_with_control(
        []() { return false; },
        nullptr,
        control
    );

    EXPECT_EQ(result.status, BusyPollStatus::Cancelled);
    EXPECT_EQ(result.data_hits, 0u);
}

TEST(BusyPoll, TimeoutStopsLoop) {
    BusyPollConfig config;
    config.poll_interval = std::chrono::microseconds(100);
    BusyPoller poller(config);

    BusyPollControl control;
    control.timeout = std::chrono::milliseconds(5);

    const auto result = poller.start_with_control(
        []() { return false; },
        nullptr,
        control
    );

    EXPECT_EQ(result.status, BusyPollStatus::Timeout);
    EXPECT_GT(result.iterations, 0u);
}

TEST(BusyPoll, StopsFromCallback) {
    BusyPoller poller;
    int count = 0;

    const auto result = poller.start_with_control(
        [&]() {
            ++count;
            if (count > 3) {
                poller.stop();
            }
            return true;
        },
        nullptr,
        BusyPollControl{}
    );

    EXPECT_EQ(result.status, BusyPollStatus::Stopped);
    EXPECT_GE(result.data_hits, 1u);
}
