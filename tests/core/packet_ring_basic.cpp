// SPDX-License-Identifier: MIT
//
// Packet Ring Buffer 基础功能测试

#include <usn/core/packet_ring.hpp>
#include <cassert>
#include <iostream>
#include <vector>

using namespace usn;

void test_basic_push_pop() {
    std::cout << "[TEST] Basic push/pop\n";
    
    PacketRing ring(1024);
    
    assert(ring.empty());
    assert(ring.size() == 0);
    assert(ring.capacity() >= 1024);
    
    // 创建测试数据
    uint8_t data[100] = {0};
    Packet pkt(data, 100, 12345);
    
    // Push
    assert(ring.try_push(pkt));
    assert(!ring.empty());
    assert(ring.size() == 1);
    
    // Pop
    Packet result;
    assert(ring.try_pop(result));
    assert(result.data == data);
    assert(result.len == 100);
    assert(result.timestamp == 12345);
    assert(ring.empty());
    
    std::cout << "  ✓ Basic push/pop passed\n";
}

void test_batch_operations() {
    std::cout << "[TEST] Batch operations\n";
    
    PacketRing ring(1024);
    const size_t batch_size = 64;
    
    // 准备测试数据
    std::vector<uint8_t> data_pool(batch_size * 100);
    std::vector<Packet> packets;
    packets.reserve(batch_size);
    
    for (size_t i = 0; i < batch_size; ++i) {
        packets.emplace_back(&data_pool[i * 100], 100, i);
    }
    
    // 批量 push
    size_t pushed = ring.try_push_batch(packets);
    assert(pushed == batch_size);
    assert(ring.size() == batch_size);
    
    // 批量 pop
    std::vector<Packet> results(batch_size);
    size_t popped = ring.try_pop_batch(results);
    assert(popped == batch_size);
    assert(ring.empty());
    
    // 验证数据
    for (size_t i = 0; i < batch_size; ++i) {
        assert(results[i].len == 100);
        assert(results[i].timestamp == i);
    }
    
    std::cout << "  ✓ Batch operations passed\n";
}

void test_ring_full() {
    std::cout << "[TEST] Ring full\n";
    
    PacketRing ring(8);
    
    uint8_t data[100] = {0};
    
    // Fill ring
    for (int i = 0; i < 8; ++i) {
        Packet pkt(data, 100, i);
        assert(ring.try_push(pkt));
    }
    
    // Try to push when full
    Packet pkt(data, 100, 999);
    assert(!ring.try_push(pkt));
    
    std::cout << "  ✓ Ring full test passed\n";
}

void test_ring_empty() {
    std::cout << "[TEST] Ring empty\n";
    
    PacketRing ring(1024);
    
    Packet result;
    assert(!ring.try_pop(result));
    
    std::cout << "  ✓ Ring empty test passed\n";
}

void test_partial_batch() {
    std::cout << "[TEST] Partial batch\n";
    
    PacketRing ring(16);
    
    // Push some packets
    uint8_t data[100] = {0};
    for (int i = 0; i < 10; ++i) {
        Packet pkt(data, 100, i);
        ring.try_push(pkt);
    }
    
    // Try to pop batch larger than available
    std::vector<Packet> results(20);
    size_t popped = ring.try_pop_batch(results);
    assert(popped == 10);
    assert(ring.empty());
    
    std::cout << "  ✓ Partial batch test passed\n";
}

int main() {
    std::cout << "Running Packet Ring Buffer tests...\n\n";
    
    try {
        test_basic_push_pop();
        test_batch_operations();
        test_ring_full();
        test_ring_empty();
        test_partial_batch();
        
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
