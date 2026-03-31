// SPDX-License-Identifier: MIT
//
// TCP 协议栈实现（简化版）
//
// 设计目标：
// - TCP 状态机
// - 序列号管理
// - 基本连接管理
// - 简化的拥塞控制

#pragma once

#include <usn/core/packet.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstdint>
#include <cstring>
#include <functional>

namespace usn {

// TCP 状态
enum class TcpState {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSE_WAIT,
    CLOSING,
    LAST_ACK,
    TIME_WAIT
};

// TCP 头部标志
struct TcpFlags {
    bool syn : 1;
    bool ack : 1;
    bool fin : 1;
    bool rst : 1;
    bool psh : 1;
    bool urg : 1;
};

// TCP 头部结构（网络字节序，packed 以避免对齐填充）
struct __attribute__((packed)) TcpHeader {
    uint16_t source_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset : 4;  // 头部长度（4字节单位）
    uint8_t reserved : 4;
    uint16_t flags;  // NS, CWR, ECE, URG, ACK, PSH, RST, SYN, FIN (低9位)
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
    
    void to_network_order() {
        source_port = htons(source_port);
        dest_port = htons(dest_port);
        seq_num = htonl(seq_num);
        ack_num = htonl(ack_num);
        window = htons(window);
        checksum = htons(checksum);
        urgent_ptr = htons(urgent_ptr);
    }
    
    void to_host_order() {
        source_port = ntohs(source_port);
        dest_port = ntohs(dest_port);
        seq_num = ntohl(seq_num);
        ack_num = ntohl(ack_num);
        window = ntohs(window);
        checksum = ntohs(checksum);
        urgent_ptr = ntohs(urgent_ptr);
    }
    
    bool has_syn() const { return (flags & (1 << 1)) != 0; }  // bit 1
    bool has_ack() const { return (flags & (1 << 4)) != 0; }  // bit 4
    bool has_fin() const { return (flags & (1 << 0)) != 0; }  // bit 0
    bool has_rst() const { return (flags & (1 << 2)) != 0; }  // bit 2
};

static_assert(sizeof(TcpHeader) >= 20, "TCP header must be at least 20 bytes");

// TCP 连接信息
struct TcpConnection {
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    
    uint32_t send_seq;      // 发送序列号
    uint32_t recv_seq;      // 接收序列号
    uint32_t send_ack;      // 发送确认号
    uint32_t recv_ack;      // 接收确认号
    
    TcpState state;
    uint16_t window_size;
    
    TcpConnection()
        : local_ip(0)
        , remote_ip(0)
        , local_port(0)
        , remote_port(0)
        , send_seq(0)
        , recv_seq(0)
        , send_ack(0)
        , recv_ack(0)
        , state(TcpState::CLOSED)
        , window_size(65535) {}
};

// TCP 协议处理类（简化版）
class TcpProtocol {
public:
    static constexpr std::size_t kHeaderLen = 20;

    // 创建 SYN 数据包（三次握手 - 第一步）
    // 调用方必须提供足够大的 out_buffer（>= kHeaderLen）
    static Packet create_syn(
        uint16_t src_port,
        uint16_t dst_port,
        uint32_t src_ip,
        uint32_t dst_ip,
        uint32_t seq_num,
        uint8_t* out_buffer
    ) {
        TcpHeader header;
        std::memset(&header, 0, sizeof(header));

        header.source_port = src_port;
        header.dest_port = dst_port;
        header.seq_num = seq_num;
        header.ack_num = 0;
        header.data_offset = 5;  // 20 bytes / 4
        header.flags = (1 << 1);  // SYN (bit 1)
        header.window = 65535;
        header.checksum = 0;
        header.urgent_ptr = 0;

        uint8_t* buffer = out_buffer;
        header.to_network_order();
        
        // 手动写入 header（正确处理 data_offset 和 flags）
        std::memcpy(buffer, &header, 12);
        buffer[12] = (header.data_offset << 4) | ((header.flags >> 8) & 0xF);
        buffer[13] = header.flags & 0xFF;
        std::memcpy(buffer + 14, reinterpret_cast<uint8_t*>(&header) + 14, 6);
        
        // 计算校验和
        uint16_t checksum = calculate_checksum(buffer, kHeaderLen, src_ip, dst_ip);
        *reinterpret_cast<uint16_t*>(buffer + 16) = htons(checksum);
        
        return Packet(buffer, kHeaderLen);
    }
    
    // 创建 SYN-ACK 数据包（三次握手 - 第二步）
    static Packet create_syn_ack(
        const TcpConnection& conn,
        uint32_t seq_num,
        uint32_t ack_num,
        uint8_t* out_buffer
    ) {
        TcpHeader header;
        std::memset(&header, 0, sizeof(header));

        header.source_port = conn.local_port;
        header.dest_port = conn.remote_port;
        header.seq_num = seq_num;
        header.ack_num = ack_num;
        header.data_offset = 5;
        header.flags = (1 << 1) | (1 << 4);  // SYN | ACK
        header.window = conn.window_size;
        header.checksum = 0;
        header.urgent_ptr = 0;

        uint8_t* buffer = out_buffer;
        header.to_network_order();
        std::memcpy(buffer, &header, 12);
        buffer[12] = (header.data_offset << 4) | ((header.flags >> 8) & 0xF);
        buffer[13] = header.flags & 0xFF;
        std::memcpy(buffer + 14, reinterpret_cast<uint8_t*>(&header) + 14, 6);
        
        header.checksum = calculate_checksum(
            buffer,
            kHeaderLen,
            conn.local_ip,
            conn.remote_ip
        );
        *reinterpret_cast<uint16_t*>(buffer + 16) = htons(header.checksum);
        
        return Packet(buffer, kHeaderLen);
    }
    
    // 创建 ACK 数据包
    static Packet create_ack(
        const TcpConnection& conn,
        uint32_t ack_num,
        uint8_t* out_buffer
    ) {
        TcpHeader header;
        std::memset(&header, 0, sizeof(header));

        header.source_port = conn.local_port;
        header.dest_port = conn.remote_port;
        header.seq_num = conn.send_seq;
        header.ack_num = ack_num;
        header.data_offset = 5;
        header.flags = (1 << 4);  // ACK
        header.window = conn.window_size;
        header.checksum = 0;
        header.urgent_ptr = 0;

        uint8_t* buffer = out_buffer;
        header.to_network_order();
        std::memcpy(buffer, &header, 12);
        buffer[12] = (header.data_offset << 4) | ((header.flags >> 8) & 0xF);
        buffer[13] = header.flags & 0xFF;
        std::memcpy(buffer + 14, reinterpret_cast<uint8_t*>(&header) + 14, 6);
        
        header.checksum = calculate_checksum(
            buffer,
            kHeaderLen,
            conn.local_ip,
            conn.remote_ip
        );
        *reinterpret_cast<uint16_t*>(buffer + 16) = htons(header.checksum);
        
        return Packet(buffer, kHeaderLen);
    }
    
    // 创建数据包（带 payload）
    // 调用方必须提供足够大的 out_buffer（>= kHeaderLen + len）
    static Packet create_data(
        const TcpConnection& conn,
        const uint8_t* data,
        std::size_t len,
        uint8_t* out_buffer
    ) {
        std::size_t total_len = kHeaderLen + len;
        uint8_t* buffer = out_buffer;
        
        TcpHeader header;
        std::memset(&header, 0, sizeof(header));
        
        header.source_port = conn.local_port;
        header.dest_port = conn.remote_port;
        header.seq_num = conn.send_seq;
        header.ack_num = conn.recv_seq;
        header.data_offset = 5;
        header.flags = (1 << 4) | (1 << 3);  // ACK | PSH
        header.window = conn.window_size;
        header.checksum = 0;
        header.urgent_ptr = 0;
        
        header.to_network_order();
        
        // 手动写入 header（正确处理 data_offset 和 flags）
        std::memcpy(buffer, &header, 12);
        buffer[12] = (header.data_offset << 4) | ((header.flags >> 8) & 0xF);
        buffer[13] = header.flags & 0xFF;
        std::memcpy(buffer + 14, reinterpret_cast<uint8_t*>(&header) + 14, 6);
        
        // 拷贝数据
        std::memcpy(buffer + kHeaderLen, data, len);
        
        // 计算校验和
        uint16_t checksum = calculate_checksum(
            buffer,
            total_len,
            conn.local_ip,
            conn.remote_ip
        );
        *reinterpret_cast<uint16_t*>(buffer + 16) = htons(checksum);
        
        return Packet(buffer, total_len);
    }
    
    // 创建 FIN 数据包
    static Packet create_fin(
        const TcpConnection& conn,
        uint8_t* out_buffer
    ) {
        TcpHeader header;
        std::memset(&header, 0, sizeof(header));

        header.source_port = conn.local_port;
        header.dest_port = conn.remote_port;
        header.seq_num = conn.send_seq;
        header.ack_num = conn.recv_seq;
        header.data_offset = 5;
        header.flags = (1 << 0) | (1 << 4);  // FIN | ACK
        header.window = conn.window_size;
        header.checksum = 0;
        header.urgent_ptr = 0;

        uint8_t* buffer = out_buffer;
        header.to_network_order();
        std::memcpy(buffer, &header, 12);
        buffer[12] = (header.data_offset << 4) | ((header.flags >> 8) & 0xF);
        buffer[13] = header.flags & 0xFF;
        std::memcpy(buffer + 14, reinterpret_cast<uint8_t*>(&header) + 14, 6);
        
        header.checksum = calculate_checksum(
            buffer,
            kHeaderLen,
            conn.local_ip,
            conn.remote_ip
        );
        *reinterpret_cast<uint16_t*>(buffer + 16) = htons(header.checksum);
        
        return Packet(buffer, kHeaderLen);
    }
    
    // 解析 TCP 数据包
    static bool parse(
        const Packet& packet,
        TcpHeader& header,
        const uint8_t*& payload,
        std::size_t& payload_len
    ) {
        if (packet.len < kHeaderLen) {  // 最小 TCP header 长度
            return false;
        }
        
        std::memset(&header, 0, sizeof(header));
        std::memcpy(&header, packet.data, 12);
        std::memcpy(reinterpret_cast<uint8_t*>(&header) + 14, packet.data + 14, 6);
        
        // 提取 data_offset（前 4 位）
        uint8_t data_offset_byte = reinterpret_cast<const uint8_t*>(packet.data)[12];
        header.data_offset = (data_offset_byte >> 4) & 0xF;
        
        // 提取 flags（后 8 位在 data_offset_byte 的低 4 位，以及下一个字节）
        header.flags = (data_offset_byte & 0xF) << 8;
        header.flags |= reinterpret_cast<const uint8_t*>(packet.data)[13];
        
        header.to_host_order();
        
        // 计算实际头部长度
        std::size_t header_len = header.data_offset * 4;
        if (header_len < kHeaderLen || header_len > packet.len) {
            return false;
        }
        
        payload = packet.data + header_len;
        payload_len = packet.len - header_len;
        
        return true;
    }
    
    // 计算 TCP 校验和（假设 src_ip / dst_ip 为网络字节序）
    static uint16_t calculate_checksum(
        const uint8_t* data,
        std::size_t len,
        uint32_t src_ip,
        uint32_t dst_ip
    ) {
        uint32_t sum = 0;
        
        // 伪头部：src_ip, dst_ip, zero(8) + protocol(8), tcp_length(16)
        uint16_t word;
        
        word = static_cast<uint16_t>(src_ip >> 16);
        sum += ntohs(word);
        word = static_cast<uint16_t>(src_ip & 0xFFFFu);
        sum += ntohs(word);
        
        word = static_cast<uint16_t>(dst_ip >> 16);
        sum += ntohs(word);
        word = static_cast<uint16_t>(dst_ip & 0xFFFFu);
        sum += ntohs(word);
        
        // zero (0) + protocol (6)
        word = 0x0006u;
        sum += ntohs(word);
        
        uint16_t tcp_len = htons(static_cast<uint16_t>(len));
        sum += ntohs(tcp_len);
        
        const uint16_t* data_words = reinterpret_cast<const uint16_t*>(data);
        std::size_t word_count = len / 2;
        for (std::size_t i = 0; i < word_count; ++i) {
            sum += ntohs(data_words[i]);
        }
        
        if (len % 2 != 0) {
            uint16_t last = static_cast<uint16_t>(data[len - 1]) << 8;
            sum += ntohs(last);
        }
        
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        
        return static_cast<uint16_t>(~sum);
    }
};

}  // namespace usn
