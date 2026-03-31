// SPDX-License-Identifier: MIT
//
// Packet Ring Buffer 使用示例

#include <usn/core/packet_ring.hpp>
#include <usn/core/memory_pool.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

using namespace usn;
using namespace std::chrono;

int main() {
    std::cout << "Packet Ring Buffer Example\n";
    std::cout << "==========================\n\n";
    
    // 创建内存池和数据包环形缓冲区
    const size_t ring_capacity = 1024;
    const size_t packet_size = 1500;  // 典型以太网 MTU
    const size_t num_packets = 100;
    
    MemoryPool pool(packet_size, num_packets);
    PacketRing ring(ring_capacity);
    
    std::cout << "Memory Pool: " << pool.num_blocks() << " blocks, "
              << pool.block_size() << " bytes each\n";
    std::cout << "Ring Buffer capacity: " << ring.capacity() << "\n\n";
    
    // 生产者：分配内存并填充数据包
    std::cout << "[Producer] Creating packets...\n";
    std::vector<Packet> packets;
    packets.reserve(num_packets);
    
    for (size_t i = 0; i < num_packets; ++i) {
        uint8_t* buffer = pool.allocate();
        if (!buffer) {
            std::cout << "  Pool exhausted at packet " << i << "\n";
            break;
        }
        
        // 填充测试数据
        std::fill(buffer, buffer + packet_size, static_cast<uint8_t>(i & 0xFF));
        
        auto now = steady_clock::now();
        auto timestamp = duration_cast<nanoseconds>(now.time_since_epoch()).count();
        
        Packet pkt(buffer, packet_size, timestamp);
        packets.push_back(pkt);
    }
    
    std::cout << "  Created " << packets.size() << " packets\n\n";
    
    // 批量推送到 ring buffer
    std::cout << "[Producer] Pushing packets to ring buffer...\n";
    size_t pushed = ring.try_push_batch(packets);
    std::cout << "  Pushed " << pushed << " packets\n";
    std::cout << "  Ring size: " << ring.size() << "\n\n";
    
    // 消费者：批量从 ring buffer 读取
    std::cout << "[Consumer] Popping packets from ring buffer...\n";
    std::vector<Packet> received(pushed);
    size_t popped = ring.try_pop_batch(received);
    std::cout << "  Popped " << popped << " packets\n";
    std::cout << "  Ring size: " << ring.size() << "\n";
    std::cout << "  Ring empty: " << (ring.empty() ? "yes" : "no") << "\n\n";
    
    // 验证数据
    std::cout << "[Verification] Checking packet data...\n";
    bool all_valid = true;
    for (size_t i = 0; i < popped; ++i) {
        if (received[i].len != packet_size) {
            std::cout << "  Packet " << i << " has wrong length\n";
            all_valid = false;
        }
        if (received[i].data[0] != static_cast<uint8_t>(i & 0xFF)) {
            std::cout << "  Packet " << i << " has wrong data\n";
            all_valid = false;
        }
    }
    
    if (all_valid) {
        std::cout << "  ✓ All packets valid\n";
    }
    
    // 释放内存回池
    std::cout << "\n[Cleanup] Returning buffers to pool...\n";
    for (auto& pkt : received) {
        pool.deallocate(pkt.data);
    }
    std::cout << "  ✓ Cleanup complete\n";
    
    return 0;
}
