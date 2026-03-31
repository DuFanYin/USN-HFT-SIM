// SPDX-License-Identifier: MIT
//
// Packet Ring Buffer 基础功能测试

#include <usn/core/packet_ring.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
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

TEST(PacketRing, CapacityRoundsUpToPowerOfTwo) {
    PacketRing ring(9);
    EXPECT_EQ(ring.capacity(), 16u);
    EXPECT_EQ(ring.available(), 16u);
}

TEST(PacketRing, WrapAroundKeepsFifoOrder) {
    PacketRing ring(4);
    std::vector<uint8_t> data_pool(8 * 32);

    for (std::size_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(ring.try_push(Packet(&data_pool[i * 32], 32, i)));
    }
    EXPECT_FALSE(ring.try_push(Packet(&data_pool[4 * 32], 32, 99)));

    for (std::size_t i = 0; i < 2; ++i) {
        Packet out;
        ASSERT_TRUE(ring.try_pop(out));
        EXPECT_EQ(out.timestamp, i);
        EXPECT_EQ(out.data, &data_pool[i * 32]);
    }

    ASSERT_TRUE(ring.try_push(Packet(&data_pool[4 * 32], 32, 4)));
    ASSERT_TRUE(ring.try_push(Packet(&data_pool[5 * 32], 32, 5)));
    EXPECT_FALSE(ring.try_push(Packet(&data_pool[6 * 32], 32, 6)));

    for (std::size_t expected = 2; expected <= 5; ++expected) {
        Packet out;
        ASSERT_TRUE(ring.try_pop(out));
        EXPECT_EQ(out.timestamp, expected);
        EXPECT_EQ(out.data, &data_pool[expected * 32]);
    }
    EXPECT_TRUE(ring.empty());
}

TEST(PacketRing, BatchPushStopsAtAvailableSpace) {
    PacketRing ring(8);
    std::vector<uint8_t> data_pool(10 * 16);
    std::vector<Packet> batch;
    batch.reserve(10);

    for (std::size_t i = 0; i < 10; ++i) {
        batch.emplace_back(&data_pool[i * 16], 16, i);
    }

    std::size_t pushed = ring.try_push_batch(batch);
    EXPECT_EQ(pushed, 8u);
    EXPECT_EQ(ring.size(), 8u);

    std::vector<Packet> popped(8);
    std::size_t pop_count = ring.try_pop_batch(popped);
    EXPECT_EQ(pop_count, 8u);
    for (std::size_t i = 0; i < pop_count; ++i) {
        EXPECT_EQ(popped[i].timestamp, i);
        EXPECT_EQ(popped[i].data, &data_pool[i * 16]);
    }
}

TEST(PacketRing, SpscThreadedTransferPreservesOrder) {
    PacketRing ring(1024);
    constexpr std::size_t kTotal = 20000;
    std::vector<uint8_t> data_pool(kTotal);
    std::atomic<bool> producer_done{false};

    std::thread producer([&]() {
        for (std::size_t i = 0; i < kTotal; ++i) {
            Packet pkt(&data_pool[i], 1, i);
            while (!ring.try_push(pkt)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::size_t expected = 0;
    while (expected < kTotal) {
        Packet out;
        if (ring.try_pop(out)) {
            ASSERT_EQ(out.timestamp, expected);
            ASSERT_EQ(out.data, &data_pool[expected]);
            ++expected;
        } else if (!producer_done.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    producer.join();
    EXPECT_TRUE(ring.empty());
}
