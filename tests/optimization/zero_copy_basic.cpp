// SPDX-License-Identifier: MIT
//
// Zero-Copy 缓冲区基础测试

#include <usn/optimization/zero_copy.hpp>
#include <cassert>
#include <iostream>
#include <cstring>

using namespace usn;

void test_zero_copy_buffer() {
    std::cout << "[TEST] Zero-Copy Buffer\n";
    
    const std::size_t size = 1024 * 1024;  // 1MB
    
    auto buffer = ZeroCopyBuffer::create(size, false);
    assert(buffer != nullptr);
    assert(buffer->size() == size);
    
    // 测试写入和读取
    uint8_t* data = buffer->data();
    std::memset(data, 0xAA, size);
    
    // 验证数据
    bool all_match = true;
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] != 0xAA) {
            all_match = false;
            break;
        }
    }
    assert(all_match);
    
    std::cout << "  ✓ Zero-Copy Buffer test passed\n";
}

void test_zero_copy_memory_pool() {
    std::cout << "[TEST] Zero-Copy Memory Pool\n";
    
    const std::size_t block_size = 1500;
    const std::size_t num_blocks = 100;
    
    ZeroCopyMemoryPool pool(block_size, num_blocks, false);
    assert(pool.block_size() == block_size);
    assert(pool.num_blocks() == num_blocks);
    
    // 分配一些块
    std::vector<uint8_t*> blocks;
    for (std::size_t i = 0; i < num_blocks; ++i) {
        uint8_t* block = pool.allocate();
        assert(block != nullptr);
        blocks.push_back(block);
        
        // 写入测试数据
        std::memset(block, static_cast<uint8_t>(i & 0xFF), block_size);
    }
    
    // 尝试分配更多（应该失败）
    assert(pool.allocate() == nullptr);
    
    // 释放一些块
    for (std::size_t i = 0; i < num_blocks / 2; ++i) {
        pool.deallocate(blocks[i]);
    }
    
    // 现在应该可以分配了
    uint8_t* new_block = pool.allocate();
    assert(new_block != nullptr);
    
    std::cout << "  ✓ Zero-Copy Memory Pool test passed\n";
}

int main() {
    std::cout << "Running Zero-Copy tests...\n\n";
    
    try {
        test_zero_copy_buffer();
        test_zero_copy_memory_pool();
        
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
