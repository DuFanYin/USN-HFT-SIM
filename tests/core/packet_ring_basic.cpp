// SPDX-License-Identifier: MIT
//
// Packet Ring Buffer 基础功能测试

#include <usn/core/packet_ring.hpp>
#include <gtest/gtest.h>
#include <vector>

using namespace usn;

TEST(PacketRing, BasicPushPop) {
    PacketRing ring(1024);

    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.size(), 0u);
    EXPECT_GE(ring.capacity(), 1024u);

    uint8_t data[100] = {0};
    Packet pkt(data, 100, 12345);

    ASSERT_TRUE(ring.try_push(pkt));
    EXPECT_FALSE(ring.empty());
    EXPECT_EQ(ring.size(), 1u);

    Packet result;
    ASSERT_TRUE(ring.try_pop(result));
    EXPECT_EQ(result.data, data);
    EXPECT_EQ(result.len, 100u);
    EXPECT_EQ(result.timestamp, 12345u);
    EXPECT_TRUE(ring.empty());
}

TEST(PacketRing, BatchOperations) {
    PacketRing ring(1024);
    const std::size_t batch_size = 64;

    std::vector<uint8_t> data_pool(batch_size * 100);
    std::vector<Packet> packets;
    packets.reserve(batch_size);

    for (std::size_t i = 0; i < batch_size; ++i) {
        packets.emplace_back(&data_pool[i * 100], 100, i);
    }

    std::size_t pushed = ring.try_push_batch(packets);
    EXPECT_EQ(pushed, batch_size);
    EXPECT_EQ(ring.size(), batch_size);

    std::vector<Packet> results(batch_size);
    std::size_t popped = ring.try_pop_batch(results);
    EXPECT_EQ(popped, batch_size);
    EXPECT_TRUE(ring.empty());

    for (std::size_t i = 0; i < batch_size; ++i) {
        EXPECT_EQ(results[i].len, 100u);
        EXPECT_EQ(results[i].timestamp, i);
    }
}

TEST(PacketRing, RingFull) {
    PacketRing ring(8);
    uint8_t data[100] = {0};

    for (int i = 0; i < 8; ++i) {
        Packet pkt(data, 100, i);
        ASSERT_TRUE(ring.try_push(pkt));
    }

    Packet pkt(data, 100, 999);
    EXPECT_FALSE(ring.try_push(pkt));
}

TEST(PacketRing, RingEmpty) {
    PacketRing ring(1024);
    Packet result;
    EXPECT_FALSE(ring.try_pop(result));
}

TEST(PacketRing, PartialBatch) {
    PacketRing ring(16);
    uint8_t data[100] = {0};

    for (int i = 0; i < 10; ++i) {
        Packet pkt(data, 100, i);
        ring.try_push(pkt);
    }

    std::vector<Packet> results(20);
    std::size_t popped = ring.try_pop_batch(results);
    EXPECT_EQ(popped, 10u);
    EXPECT_TRUE(ring.empty());
}
