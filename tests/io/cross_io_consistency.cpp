// SPDX-License-Identifier: MIT
//
// Cross-backend semantic consistency tests.

#include <usn/io/io_status.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <vector>

using namespace usn;

TEST(CrossIOConsistency, TimeoutSemanticsMatchAcrossBackends) {
    std::vector<UnifiedIOResult> results;

    EpollWrapper epoll;
    std::vector<epoll_event> events;
    EpollWaitOptions epoll_opts;
    epoll_opts.timeout_ms = 1;
    results.push_back(to_unified_result(epoll.wait_with_options(events, epoll_opts)));

    IoUringWrapper uring(8);
    if (uring.is_initialized()) {
        std::vector<io_uring_cqe*> cqes;
        IoUringWaitOptions uring_opts;
        uring_opts.timeout_ms = 1;
        results.push_back(to_unified_result(uring.wait_completions_with_options(cqes, 1, 8, uring_opts)));
    }

    BusyPollConfig poll_cfg;
    poll_cfg.poll_interval = std::chrono::microseconds(100);
    BusyPoller poller(poll_cfg);
    BusyPollControl poll_ctl;
    poll_ctl.timeout = std::chrono::milliseconds(2);
    results.push_back(to_unified_result(poller.start_with_control([]() { return false; }, nullptr, poll_ctl)));

    ASSERT_GE(results.size(), 2u);
    for (const auto& r : results) {
        EXPECT_EQ(r.status, UnifiedIOStatus::Timeout);
        EXPECT_EQ(classify_loop_control(r), LoopControl::ContinueIdle);
    }
}

TEST(CrossIOConsistency, CancelSemanticsMatchAcrossBackends) {
    std::vector<UnifiedIOResult> results;
    std::atomic<bool> cancelled{true};

    EpollWrapper epoll;
    std::vector<epoll_event> events;
    EpollWaitOptions epoll_opts;
    epoll_opts.cancel_flag = &cancelled;
    results.push_back(to_unified_result(epoll.wait_with_options(events, epoll_opts)));

    IoUringWrapper uring(8);
    if (uring.is_initialized()) {
        std::vector<io_uring_cqe*> cqes;
        IoUringWaitOptions uring_opts;
        uring_opts.cancel_flag = &cancelled;
        results.push_back(to_unified_result(uring.wait_completions_with_options(cqes, 1, 8, uring_opts)));
    }

    BusyPoller poller;
    BusyPollControl poll_ctl;
    poll_ctl.cancel_flag = &cancelled;
    results.push_back(to_unified_result(poller.start_with_control([]() { return false; }, nullptr, poll_ctl)));

    ASSERT_GE(results.size(), 2u);
    for (const auto& r : results) {
        EXPECT_EQ(r.status, UnifiedIOStatus::Cancelled);
        EXPECT_EQ(classify_loop_control(r), LoopControl::Stop);
    }
}
