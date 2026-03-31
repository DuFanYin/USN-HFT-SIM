// SPDX-License-Identifier: MIT
//
// MPSC Queue 性能基准测试

#include <usn/core/mpsc_queue.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace usn;
using namespace std::chrono;

void benchmark_single_threaded() {
    std::cout << "[BENCH] Single-threaded push/pop\n";
    
    MpscQueue<int> queue(1024 * 1024);
    const int iterations = 10'000'000;
    
    // Warmup
    for (int i = 0; i < 1000; ++i) {
        queue.try_push(i);
        queue.try_pop();
    }
    
    auto start = steady_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        queue.try_push(i);
        queue.try_pop();
    }
    
    auto end = steady_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start);
    
    double ops_per_sec = (iterations * 2.0) / duration.count() * 1e9;
    double ns_per_op = duration.count() / (iterations * 2.0);
    
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Duration: " << duration.count() / 1e6 << " ms\n";
    std::cout << "  Throughput: " << ops_per_sec / 1e6 << " M ops/sec\n";
    std::cout << "  Latency: " << ns_per_op << " ns/op\n\n";
}

void benchmark_multithreaded() {
    std::cout << "[BENCH] Multi-producer single-consumer\n";
    
    MpscQueue<int> queue(1024 * 1024);
    const int num_producers = 4;
    const int items_per_producer = 1'000'000;
    
    std::vector<std::thread> producers;
    std::atomic<int> total_pushed{0};
    
    auto start = steady_clock::now();
    
    // 启动生产者
    for (int t = 0; t < num_producers; ++t) {
        producers.emplace_back([&queue, &total_pushed]() {
            for (int i = 0; i < items_per_producer; ++i) {
                while (!queue.try_push(i)) {
                    std::this_thread::yield();
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    // 消费者
    int total_received = 0;
    while (total_received < num_producers * items_per_producer) {
        auto value = queue.try_pop();
        if (value.has_value()) {
            total_received++;
        } else {
            std::this_thread::yield();
        }
    }
    
    auto end = steady_clock::now();
    
    // 等待生产者完成
    for (auto& t : producers) {
        t.join();
    }
    
    auto duration = duration_cast<nanoseconds>(end - start);
    int total_ops = num_producers * items_per_producer * 2;  // push + pop
    
    double ops_per_sec = total_ops / duration.count() * 1e9;
    double ns_per_op = duration.count() / static_cast<double>(total_ops);
    
    std::cout << "  Producers: " << num_producers << "\n";
    std::cout << "  Items per producer: " << items_per_producer << "\n";
    std::cout << "  Total items: " << total_ops / 2 << "\n";
    std::cout << "  Duration: " << duration.count() / 1e6 << " ms\n";
    std::cout << "  Throughput: " << ops_per_sec / 1e6 << " M ops/sec\n";
    std::cout << "  Latency: " << ns_per_op << " ns/op\n\n";
}

int main() {
    std::cout << "MPSC Queue Performance Benchmarks\n";
    std::cout << "==================================\n\n";
    
    benchmark_single_threaded();
    benchmark_multithreaded();
    
    return 0;
}
