// SPDX-License-Identifier: MIT
//
// Zero-Copy 缓冲区基础测试

#include <usn/optimization/zero_copy.hpp>
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace usn;

TEST(ZeroCopyBuffer, CreateAndReadWrite) {
    const std::size_t size = 1024 * 1024;  // 1MB

    auto buffer = ZeroCopyBuffer::create(size, false);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->size(), size);

    uint8_t* data = buffer->data();
    std::memset(data, 0xAA, size);

    for (std::size_t i = 0; i < size; ++i) {
        ASSERT_EQ(data[i], 0xAA) << "Mismatch at index " << i;
    }
}

TEST(ZeroCopyMemoryPool, AllocateDeallocate) {
    const std::size_t block_size = 1500;
    const std::size_t num_blocks = 100;

    ZeroCopyMemoryPool pool(block_size, num_blocks, false);
    EXPECT_EQ(pool.block_size(), block_size);
    EXPECT_EQ(pool.num_blocks(), num_blocks);

    std::vector<uint8_t*> blocks;
    for (std::size_t i = 0; i < num_blocks; ++i) {
        uint8_t* block = pool.allocate();
        ASSERT_NE(block, nullptr) << "Allocation failed at block " << i;
        blocks.push_back(block);
        std::memset(block, static_cast<uint8_t>(i & 0xFF), block_size);
    }

    // 池已满
    EXPECT_EQ(pool.allocate(), nullptr);

    // 释放一半
    for (std::size_t i = 0; i < num_blocks / 2; ++i) {
        pool.deallocate(blocks[i]);
    }

    // 应该能再分配
    EXPECT_NE(pool.allocate(), nullptr);
}
