// SPDX-License-Identifier: MIT

#include <usn/core/tracing.hpp>
#include <gtest/gtest.h>

using namespace usn;

TEST(Tracing, SamplingPolicy) {
    Tracer t(TraceConfig{true, 2});
    EXPECT_FALSE(t.should_emit());
    EXPECT_TRUE(t.should_emit());
    EXPECT_FALSE(t.should_emit());
    EXPECT_TRUE(t.should_emit());
}

TEST(Tracing, DisabledNeverEmits) {
    Tracer t(TraceConfig{false, 1});
    EXPECT_FALSE(t.should_emit());
    EXPECT_FALSE(t.should_emit());
}
