// SPDX-License-Identifier: MIT
//
// UDP 协议栈实现
//
// 设计目标：
// - UDP 数据包封装/解析
// - 校验和计算
// - 端口管理
// - 与 Packet Ring Buffer 集成

#pragma once

#include <usn/core/packet.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace usn {

// UDP 头部结构（网络字节序）
struct alignas(2) UdpHeader {
    uint16_t source_port;      // 源端口
    uint16_t dest_port;        // 目标端口
    uint16_t length;           // UDP 长度（头部+数据）
    uint16_t checksum;        // 校验和
    
    // 网络字节序转换
    void to_network_order() {
        source_port = htons(source_port);
        dest_port = htons(dest_port);
        length = htons(length);
        checksum = htons(checksum);
    }
    
    void to_host_order() {
        source_port = ntohs(source_port);
        dest_port = ntohs(dest_port);
        length = ntohs(length);
        checksum = ntohs(checksum);
    }
};

static_assert(sizeof(UdpHeader) == 8, "UDP header must be 8 bytes");

// UDP 协议处理类
class UdpProtocol {
public:
    // 计算 UDP 校验和（伪头部 + UDP 头部 + 数据）
    // 注意：这里假设 src_ip / dst_ip 已经是网络字节序（例如来自 in_addr.s_addr）
    static uint16_t calculate_checksum(
        const uint8_t* data,
        std::size_t len,
        uint32_t src_ip,
        uint32_t dst_ip
    ) {
        // 计算校验和
        uint32_t sum = 0;
        
        // 伪头部：src_ip, dst_ip, zero(8) + protocol(8), udp_length(16)
        uint16_t word;
        
        word = static_cast<uint16_t>(src_ip >> 16);
        sum += ntohs(word);
        word = static_cast<uint16_t>(src_ip & 0xFFFFu);
        sum += ntohs(word);
        
        word = static_cast<uint16_t>(dst_ip >> 16);
        sum += ntohs(word);
        word = static_cast<uint16_t>(dst_ip & 0xFFFFu);
        sum += ntohs(word);
        
        // zero (0) + protocol (17)
        word = 0x0011u;  // high byte zero, low byte = 17
        sum += ntohs(word);
        
        // UDP 长度字段
        uint16_t udp_len = htons(static_cast<uint16_t>(len));
        sum += ntohs(udp_len);
        
        // UDP 头部 + 数据（如果长度为奇数，补零）
        const uint16_t* data_words = reinterpret_cast<const uint16_t*>(data);
        std::size_t word_count = len / 2;
        for (std::size_t i = 0; i < word_count; ++i) {
            sum += ntohs(data_words[i]);
        }
        
        if (len % 2 != 0) {
            uint16_t last = static_cast<uint16_t>(data[len - 1]) << 8;
            sum += ntohs(last);
        }
        
        // 折叠到 16 位
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        
        return static_cast<uint16_t>(~sum);
    }
    
    // 封装 UDP 数据包
    // 返回封装后的数据包（包含 UDP 头部）
    static Packet encapsulate(
        const uint8_t* payload,
        std::size_t payload_len,
        uint16_t src_port,
        uint16_t dst_port,
        uint32_t src_ip = 0,
        uint32_t dst_ip = 0,
        bool calculate_checksum_flag = true
    ) {
        // 分配缓冲区（UDP 头部 + 数据）
        std::size_t total_len = sizeof(UdpHeader) + payload_len;
        uint8_t* buffer = new uint8_t[total_len];
        
        // 构建 UDP 头部
        UdpHeader header;
        header.source_port = src_port;
        header.dest_port = dst_port;
        header.length = static_cast<uint16_t>(total_len);
        header.checksum = 0;  // 先设为 0
        
        // 拷贝头部（主机字节序）
        std::memcpy(buffer, &header, sizeof(UdpHeader));
        
        // 拷贝数据
        std::memcpy(buffer + sizeof(UdpHeader), payload, payload_len);
        
        // 转换为网络字节序
        header.to_network_order();
        std::memcpy(buffer, &header, sizeof(UdpHeader));
        
        // 计算校验和（如果需要）
        if (calculate_checksum_flag && src_ip != 0 && dst_ip != 0) {
            uint16_t checksum = calculate_checksum(
                buffer,
                total_len,
                src_ip,
                dst_ip
            );
            // 更新校验和（网络字节序）
            *reinterpret_cast<uint16_t*>(buffer + offsetof(UdpHeader, checksum)) = htons(checksum);
        }
        
        return Packet(buffer, total_len);
    }
    
    // 解析 UDP 数据包
    // 返回解析结果：成功返回 true，失败返回 false
    static bool parse(
        const Packet& packet,
        UdpHeader& header,
        const uint8_t*& payload,
        std::size_t& payload_len
    ) {
        if (packet.len < sizeof(UdpHeader)) {
            return false;  // 数据包太短
        }
        
        // 解析头部
        std::memcpy(&header, packet.data, sizeof(UdpHeader));
        header.to_host_order();
        
        // 验证长度
        if (header.length < sizeof(UdpHeader) || header.length > packet.len) {
            return false;
        }
        
        // 提取 payload
        payload = packet.data + sizeof(UdpHeader);
        payload_len = header.length - sizeof(UdpHeader);
        
        return true;
    }
    
    // 验证 UDP 校验和
    static bool verify_checksum(
        const Packet& packet,
        uint32_t src_ip,
        uint32_t dst_ip
    ) {
        if (packet.len < sizeof(UdpHeader)) {
            return false;
        }
        
        UdpHeader header;
        std::memcpy(&header, packet.data, sizeof(UdpHeader));
        header.to_host_order();
        
        // 计算校验和
        uint16_t calculated = calculate_checksum(
            packet.data,
            packet.len,
            src_ip,
            dst_ip
        );
        
        // 校验和应该为 0（如果正确）
        return calculated == 0;
    }
};

}  // namespace usn
