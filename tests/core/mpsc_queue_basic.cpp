// SPDX-License-Identifier: MIT
//
// MPSC Queue 基础功能测试

#include <usn/core/mpsc_queue.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <thread>
#include <vector>

using namespace usn;

TEST(MpscQueue, BasicPushPop) {
    MpscQueue<int> queue(1024);

    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_GE(queue.capacity(), 1024u);

    ASSERT_TRUE(queue.try_push(42));
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1u);

    auto value = queue.try_pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(MpscQueue, MultipleElements) {
    MpscQueue<int> queue(1024);
    const int count = 100;

    for (int i = 0; i < count; ++i) {
        ASSERT_TRUE(queue.try_push(i));
    }
    EXPECT_EQ(queue.size(), static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        auto value = queue.try_pop();
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, i);
    }
    EXPECT_TRUE(queue.empty());
}

TEST(MpscQueue, QueueFull) {
    MpscQueue<int> queue(8);

    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(queue.try_push(i));
    }
    EXPECT_FALSE(queue.try_push(999));
}

TEST(MpscQueue, QueueEmpty) {
    MpscQueue<int> queue(1024);

    auto value = queue.try_pop();
    EXPECT_FALSE(value.has_value());
}

TEST(MpscQueue, MultithreadedProducers) {
    MpscQueue<int> queue(1024);
    const int num_producers = 4;
    const int items_per_producer = 1000;

    std::vector<std::thread> producers;
    std::atomic<int> total_pushed{0};

    for (int t = 0; t < num_producers; ++t) {
        producers.emplace_back([&queue, &total_pushed, t]() {
            for (int i = 0; i < items_per_producer; ++i) {
                int value = t * items_per_producer + i;
                while (!queue.try_push(value)) {
                    std::this_thread::yield();
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<int> received;
    received.reserve(num_producers * items_per_producer);

    while (received.size() < static_cast<std::size_t>(num_producers * items_per_producer)) {
        auto value = queue.try_pop();
        if (value.has_value()) {
            received.push_back(*value);
        } else {
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    ASSERT_EQ(received.size(), static_cast<std::size_t>(num_producers * items_per_producer));

    std::sort(received.begin(), received.end());
    for (std::size_t i = 0; i < received.size(); ++i) {
        EXPECT_EQ(received[i], static_cast<int>(i));
    }
}
