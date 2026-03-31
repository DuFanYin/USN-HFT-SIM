// SPDX-License-Identifier: MIT
//
// io_uring 基础测试

#include <usn/io/io_uring_wrapper.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <vector>

using namespace usn;

TEST(IoUring, InitializationOrSkip) {
    IoUringWrapper uring(8);
    if (!uring.is_initialized()) {
        GTEST_SKIP() << "io_uring not available in this environment";
    }
    EXPECT_TRUE(uring.is_initialized());
}

TEST(IoUring, TimeoutWhenNoCompletion) {
    IoUringWrapper uring(8);
    if (!uring.is_initialized()) {
        GTEST_SKIP() << "io_uring not available in this environment";
    }

    std::vector<io_uring_cqe*> cqes;
    IoUringWaitOptions options;
    options.timeout_ms = 1;
    const auto result = uring.wait_completions_with_options(cqes, 1, 8, options);
    EXPECT_EQ(result.count, 0);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(result.status, IoUringWaitStatus::Timeout);
}

TEST(IoUring, CancelledBeforeWait) {
    IoUringWrapper uring(8);
    if (!uring.is_initialized()) {
        GTEST_SKIP() << "io_uring not available in this environment";
    }

    std::atomic<bool> cancelled{true};
    std::vector<io_uring_cqe*> cqes;
    IoUringWaitOptions options;
    options.cancel_flag = &cancelled;
    const auto result = uring.wait_completions_with_options(cqes, 1, 8, options);
    EXPECT_EQ(result.count, 0);
    EXPECT_EQ(result.error, 0);
    EXPECT_EQ(result.status, IoUringWaitStatus::Cancelled);
}
