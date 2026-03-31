// SPDX-License-Identifier: MIT
//
// UDP feedback metrics basic tests

#include <usn/protocol/udp_feedback.hpp>
#include <gtest/gtest.h>

using namespace usn;

TEST(UdpFeedback, InOrder) {
    UdpDeliveryFeedback f;
    f.observe(1);
    f.observe(2);
    f.observe(3);
    EXPECT_EQ(f.total, 3u);
    EXPECT_EQ(f.gaps, 0u);
    EXPECT_EQ(f.reorder_events, 0u);
    EXPECT_EQ(f.expected_seq, 4u);
}

TEST(UdpFeedback, GapAndReorder) {
    UdpDeliveryFeedback f;
    f.observe(1);
    f.observe(4);  // gap: 2,3
    f.observe(3);  // reorder
    EXPECT_EQ(f.total, 3u);
    EXPECT_EQ(f.gaps, 2u);
    EXPECT_EQ(f.reorder_events, 1u);
    EXPECT_GE(f.max_reorder_depth, 1u);
}
