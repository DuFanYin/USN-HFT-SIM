// SPDX-License-Identifier: MIT
//
// 数据包元数据结构定义
//
// 设计目标：
// - 零拷贝：存储指向数据的指针，不拷贝数据本身
// - Cache-aligned：避免 false sharing
// - 元数据完整：包含长度、时间戳等信息

#pragma once

#include <cstddef>
#include <cstdint>

namespace usn {

// Cache line 大小（通常 64 bytes）
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

// 数据包元数据结构
// 注意：这个结构体需要 cache-aligned，避免 false sharing
struct alignas(CACHE_LINE_SIZE) Packet {
    uint8_t* data;           // 指向数据包数据的指针（零拷贝）
    std::size_t len;         // 数据包长度（bytes）
    uint64_t timestamp;      // 时间戳（纳秒）
    uint16_t port;           // 端口号（可选）
    uint32_t flags;          // 标志位（可选）
    
    Packet() noexcept
        : data(nullptr)
        , len(0)
        , timestamp(0)
        , port(0)
        , flags(0) {}
    
    Packet(uint8_t* d, std::size_t l, uint64_t ts = 0) noexcept
        : data(d)
        , len(l)
        , timestamp(ts)
        , port(0)
        , flags(0) {}
    
    // 重置数据包
    void reset() noexcept {
        data = nullptr;
        len = 0;
        timestamp = 0;
        port = 0;
        flags = 0;
    }
    
    // 检查数据包是否有效
    bool valid() const noexcept {
        return data != nullptr && len > 0;
    }
};

static_assert(sizeof(Packet) >= CACHE_LINE_SIZE, "Packet must be cache-aligned");
static_assert(alignof(Packet) == CACHE_LINE_SIZE, "Packet alignment must match cache line size");

}  // namespace usn
