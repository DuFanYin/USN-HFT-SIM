// SPDX-License-Identifier: MIT
//
// Unified IO status mapping tests

#include <usn/io/io_status.hpp>
#include <gtest/gtest.h>

using namespace usn;

TEST(IOStatus, BatchMapping) {
    BatchIOResult src{3, 0, BatchIOStatus::Ok};
    const auto dst = to_unified_result(src);
    EXPECT_EQ(dst.count, 3u);
    EXPECT_EQ(dst.error, 0);
    EXPECT_EQ(dst.status, UnifiedIOStatus::Ok);
}

TEST(IOStatus, EpollMapping) {
    EpollWaitResult src{0, 0, EpollWaitStatus::Timeout};
    const auto dst = to_unified_result(src);
    EXPECT_EQ(dst.count, 0u);
    EXPECT_EQ(dst.status, UnifiedIOStatus::Timeout);
}

TEST(IOStatus, IoUringMapping) {
    IoUringWaitResult src{0, 0, IoUringWaitStatus::NotInitialized};
    const auto dst = to_unified_result(src);
    EXPECT_EQ(dst.count, 0u);
    EXPECT_EQ(dst.status, UnifiedIOStatus::NotInitialized);
}

TEST(IOStatus, BusyPollMapping) {
    BusyPollResult src{};
    src.data_hits = 7;
    src.status = BusyPollStatus::Stopped;
    const auto dst = to_unified_result(src);
    EXPECT_EQ(dst.count, 7u);
    EXPECT_EQ(dst.status, UnifiedIOStatus::Stopped);
}

TEST(IOStatus, LoopControlClassification) {
    EXPECT_EQ(classify_loop_control(UnifiedIOResult{1, 0, UnifiedIOStatus::Ok}), LoopControl::ContinueWork);
    EXPECT_EQ(classify_loop_control(UnifiedIOResult{0, 0, UnifiedIOStatus::Timeout}), LoopControl::ContinueIdle);
    EXPECT_EQ(classify_loop_control(UnifiedIOResult{0, EIO, UnifiedIOStatus::SysError}), LoopControl::Stop);
}
