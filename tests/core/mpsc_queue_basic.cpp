// SPDX-License-Identifier: MIT
//
// MPSC Queue 基础功能测试

#include <usn/core/mpsc_queue.hpp>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace usn;

void test_basic_push_pop() {
    std::cout << "[TEST] Basic push/pop\n";
    
    MpscQueue<int> queue(1024);
    
    assert(queue.empty());
    assert(queue.size() == 0);
    assert(queue.capacity() >= 1024);
    
    // Push
    assert(queue.try_push(42));
    assert(!queue.empty());
    assert(queue.size() == 1);
    
    // Pop
    auto value = queue.try_pop();
    assert(value.has_value());
    assert(*value == 42);
    assert(queue.empty());
    
    std::cout << "  ✓ Basic push/pop passed\n";
}

void test_multiple_elements() {
    std::cout << "[TEST] Multiple elements\n";
    
    MpscQueue<int> queue(1024);
    const int count = 100;
    
    // Push multiple
    for (int i = 0; i < count; ++i) {
        assert(queue.try_push(i));
    }
    
    assert(queue.size() == count);
    
    // Pop all
    for (int i = 0; i < count; ++i) {
        auto value = queue.try_pop();
        assert(value.has_value());
        assert(*value == i);
    }
    
    assert(queue.empty());
    
    std::cout << "  ✓ Multiple elements passed\n";
}

void test_queue_full() {
    std::cout << "[TEST] Queue full\n";
    
    MpscQueue<int> queue(8);
    
    // Fill queue
    for (int i = 0; i < 8; ++i) {
        assert(queue.try_push(i));
    }
    
    // Try to push when full
    assert(!queue.try_push(999));
    
    std::cout << "  ✓ Queue full test passed\n";
}

void test_queue_empty() {
    std::cout << "[TEST] Queue empty\n";
    
    MpscQueue<int> queue(1024);
    
    auto value = queue.try_pop();
    assert(!value.has_value());
    
    std::cout << "  ✓ Queue empty test passed\n";
}

void test_multithreaded_producers() {
    std::cout << "[TEST] Multithreaded producers\n";
    
    MpscQueue<int> queue(1024);
    const int num_producers = 4;
    const int items_per_producer = 1000;
    
    std::vector<std::thread> producers;
    std::atomic<int> total_pushed{0};
    
    // 启动生产者线程
    for (int t = 0; t < num_producers; ++t) {
        producers.emplace_back([&queue, &total_pushed, t]() {
            for (int i = 0; i < items_per_producer; ++i) {
                int value = t * items_per_producer + i;
                while (!queue.try_push(value)) {
                    // 队列满时重试
                    std::this_thread::yield();
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    // 消费者线程
    std::vector<int> received;
    received.reserve(num_producers * items_per_producer);
    
    while (received.size() < num_producers * items_per_producer) {
        auto value = queue.try_pop();
        if (value.has_value()) {
            received.push_back(*value);
        } else {
            std::this_thread::yield();
        }
    }
    
    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }
    
    // 验证：应该收到所有数据
    assert(received.size() == num_producers * items_per_producer);
    
    // 验证：数据完整性（排序后应该是连续的）
    std::sort(received.begin(), received.end());
    for (size_t i = 0; i < received.size(); ++i) {
        assert(received[i] == static_cast<int>(i));
    }
    
    std::cout << "  ✓ Multithreaded producers passed\n";
}

int main() {
    std::cout << "Running MPSC Queue tests...\n\n";
    
    try {
        test_basic_push_pop();
        test_multiple_elements();
        test_queue_full();
        test_queue_empty();
        test_multithreaded_producers();
        
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
