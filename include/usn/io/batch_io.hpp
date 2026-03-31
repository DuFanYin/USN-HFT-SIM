// SPDX-License-Identifier: MIT
//
// Batch I/O - 批量网络 I/O 操作
//
// 设计目标：
// - 封装 recvmmsg/sendmmsg 系统调用
// - 批量接收/发送数据包
// - 减少系统调用开销
// - 与 Packet Ring Buffer 集成
//
// 注意：需要 Linux 系统（recvmmsg/sendmmsg 是 Linux 特有）

#pragma once

#include <usn/core/packet.hpp>
#include <usn/core/packet_ring.hpp>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/socket.h>
#include <cerrno>
#include <cstddef>
#include <span>
#include <vector>

namespace usn {

// Batch I/O 结果
struct BatchIOResult {
    std::size_t count;      // 成功处理的数量
    int error;              // 错误码（如果有）
    
    bool success() const noexcept {
        return error == 0;
    }
};

// Batch Recv - 批量接收 UDP 数据包
class BatchRecv {
public:
    explicit BatchRecv(int socket_fd)
        : socket_fd_(socket_fd) {
    }
    
    // 批量接收数据包到 ring buffer
    // 返回实际接收的数量
    BatchIOResult recv_batch(PacketRing& ring, std::size_t max_packets) {
        if (max_packets == 0) {
            return {0, 0};
        }
        
        // 准备临时缓冲区用于接收
        if (temp_packets_.size() < max_packets) {
            temp_packets_.resize(max_packets);
        }
        
        if (temp_buffers_.size() < max_packets) {
            temp_buffers_.resize(max_packets);
            temp_msghdrs_.resize(max_packets);
            temp_iovecs_.resize(max_packets);
        }
        
        // 准备 iovec 和 msghdr
        for (std::size_t i = 0; i < max_packets; ++i) {
            temp_buffers_[i].resize(65536);  // 最大 UDP 包大小
            temp_iovecs_[i].iov_base = temp_buffers_[i].data();
            temp_iovecs_[i].iov_len = temp_buffers_[i].size();
            
            temp_msghdrs_[i].msg_hdr.msg_iov = &temp_iovecs_[i];
            temp_msghdrs_[i].msg_hdr.msg_iovlen = 1;
            temp_msghdrs_[i].msg_hdr.msg_name = nullptr;
            temp_msghdrs_[i].msg_hdr.msg_namelen = 0;
            temp_msghdrs_[i].msg_hdr.msg_control = nullptr;
            temp_msghdrs_[i].msg_hdr.msg_controllen = 0;
            temp_msghdrs_[i].msg_hdr.msg_flags = 0;
        }
        
        // 使用 recvmmsg（Linux）
        const unsigned int msg_count = static_cast<unsigned int>(max_packets);
        int received = recvmmsg(
            socket_fd_,
            temp_msghdrs_.data(),
            msg_count,
            MSG_DONTWAIT,  // 非阻塞
            nullptr
        );
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return {0, 0};  // 没有数据可读，不是错误
            }
            return {0, errno};
        }
        
        // 将接收到的数据包添加到 ring buffer
        std::size_t added = 0;
        for (int i = 0; i < received; ++i) {
            Packet pkt(
                temp_buffers_[i].data(),
                temp_msghdrs_[i].msg_len,
                0  // TODO: 添加时间戳
            );
            
            if (ring.try_push(pkt)) {
                added++;
            } else {
                // Ring buffer 满，停止添加
                break;
            }
        }
        
        return {added, 0};
    }
    
    // 批量接收数据包到数组
    BatchIOResult recv_batch(std::span<Packet> packets) {
        if (packets.empty()) {
            return {0, 0};
        }
        
        // 准备缓冲区
        if (temp_buffers_.size() < packets.size()) {
            temp_buffers_.resize(packets.size());
            temp_msghdrs_.resize(packets.size());
            temp_iovecs_.resize(packets.size());
        }
        
        for (std::size_t i = 0; i < packets.size(); ++i) {
            temp_buffers_[i].resize(65536);
            temp_iovecs_[i].iov_base = temp_buffers_[i].data();
            temp_iovecs_[i].iov_len = temp_buffers_[i].size();
            
            temp_msghdrs_[i].msg_hdr.msg_iov = &temp_iovecs_[i];
            temp_msghdrs_[i].msg_hdr.msg_iovlen = 1;
            temp_msghdrs_[i].msg_hdr.msg_name = nullptr;
            temp_msghdrs_[i].msg_hdr.msg_namelen = 0;
            temp_msghdrs_[i].msg_hdr.msg_control = nullptr;
            temp_msghdrs_[i].msg_hdr.msg_controllen = 0;
            temp_msghdrs_[i].msg_hdr.msg_flags = 0;
        }
        
        const unsigned int msg_count = static_cast<unsigned int>(packets.size());
        int received = recvmmsg(
            socket_fd_,
            temp_msghdrs_.data(),
            msg_count,
            MSG_DONTWAIT,
            nullptr
        );
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return {0, 0};
            }
            return {0, errno};
        }
        
        for (int i = 0; i < received; ++i) {
            packets[i] = Packet(
                temp_buffers_[i].data(),
                temp_msghdrs_[i].msg_len,
                0
            );
        }
        
        return {static_cast<std::size_t>(received), 0};
    }

private:
    int socket_fd_;
    std::vector<Packet> temp_packets_;
    std::vector<std::vector<uint8_t>> temp_buffers_;
    std::vector<struct mmsghdr> temp_msghdrs_;
    std::vector<struct iovec> temp_iovecs_;
};

// Batch Send - 批量发送 UDP 数据包
class BatchSend {
public:
    explicit BatchSend(int socket_fd)
        : socket_fd_(socket_fd) {
    }
    
    // 批量发送数据包
    BatchIOResult send_batch(std::span<const Packet> packets) {
        if (packets.empty()) {
            return {0, 0};
        }
        
        // 准备 msghdr 和 iovec
        if (temp_msghdrs_.size() < packets.size()) {
            temp_msghdrs_.resize(packets.size());
            temp_iovecs_.resize(packets.size());
        }
        
        for (std::size_t i = 0; i < packets.size(); ++i) {
            temp_iovecs_[i].iov_base = packets[i].data;
            temp_iovecs_[i].iov_len = packets[i].len;
            
            temp_msghdrs_[i].msg_hdr.msg_iov = &temp_iovecs_[i];
            temp_msghdrs_[i].msg_hdr.msg_iovlen = 1;
            temp_msghdrs_[i].msg_hdr.msg_name = nullptr;
            temp_msghdrs_[i].msg_hdr.msg_namelen = 0;
            temp_msghdrs_[i].msg_hdr.msg_control = nullptr;
            temp_msghdrs_[i].msg_hdr.msg_controllen = 0;
            temp_msghdrs_[i].msg_hdr.msg_flags = 0;
        }
        
        // 使用 sendmmsg（Linux）
        const unsigned int msg_count = static_cast<unsigned int>(packets.size());
        int sent = sendmmsg(
            socket_fd_,
            temp_msghdrs_.data(),
            msg_count,
            MSG_DONTWAIT
        );
        
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return {0, 0};  // 发送缓冲区满，不是错误
            }
            return {0, errno};
        }
        
        return {static_cast<std::size_t>(sent), 0};
    }

private:
    int socket_fd_;
    std::vector<struct mmsghdr> temp_msghdrs_;
    std::vector<struct iovec> temp_iovecs_;
};

}  // namespace usn
