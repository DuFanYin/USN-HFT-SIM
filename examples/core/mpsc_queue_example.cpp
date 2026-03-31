// SPDX-License-Identifier: MIT
//
// MPSC Queue 使用示例

#include <usn/core/mpsc_queue.hpp>
#include <iostream>
#include <thread>
#include <vector>

using namespace usn;

int main() {
    // 创建一个容量为 1024 的队列
    MpscQueue<int> queue(1024);
    
    // 启动多个生产者线程
    const int num_producers = 3;
    std::vector<std::thread> producers;
    
    for (int t = 0; t < num_producers; ++t) {
        producers.emplace_back([&queue, t]() {
            for (int i = 0; i < 10; ++i) {
                int value = t * 100 + i;
                if (queue.try_push(value)) {
                    std::cout << "Producer " << t << " pushed: " << value << "\n";
                } else {
                    std::cout << "Producer " << t << " failed to push (queue full)\n";
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // 单消费者线程
    int total_received = 0;
    while (total_received < num_producers * 10) {
        auto value = queue.try_pop();
        if (value.has_value()) {
            std::cout << "Consumer received: " << *value << "\n";
            total_received++;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }
    
    std::cout << "\nTotal received: " << total_received << "\n";
    std::cout << "Queue size: " << queue.size() << "\n";
    std::cout << "Queue empty: " << (queue.empty() ? "yes" : "no") << "\n";
    
    return 0;
}
