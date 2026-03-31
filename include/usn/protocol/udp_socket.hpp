// SPDX-License-Identifier: MIT
//
// UDP Socket 封装
//
// 设计目标：
// - 类似标准 socket API 的接口
// - 与 Packet Ring Buffer 集成
// - 支持批量操作

#pragma once

#include <usn/protocol/udp_protocol.hpp>
#include <usn/core/packet_ring.hpp>
#include <usn/io/batch_io.hpp>
#include <usn/io/io_status.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <span>

namespace usn {

// UDP Socket 类
class UdpSocket {
public:
    UdpSocket()
        : fd_(-1)
        , bound_(false) {
    }
    
    ~UdpSocket() {
        close();
    }
    
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    
    // 创建 socket
    bool create() {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) {
            return false;
        }
        
        // 设置为非阻塞
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        
        return true;
    }
    
    // 绑定到端口
    bool bind(uint16_t port, const char* ip = nullptr) {
        if (fd_ < 0 && !create()) {
            return false;
        }
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (ip) {
            if (inet_aton(ip, &addr.sin_addr) == 0) {
                return false;
            }
        } else {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
        
        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            return false;
        }
        
        bound_ = true;
        return true;
    }
    
    // 发送单个数据包
    ssize_t sendto(
        const uint8_t* data,
        std::size_t len,
        const char* ip,
        uint16_t port
    ) {
        if (fd_ < 0 && !create()) {
            return -1;
        }
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_aton(ip, &addr.sin_addr) == 0) {
            return -1;
        }
        
        return ::sendto(
            fd_,
            data,
            len,
            0,
            reinterpret_cast<struct sockaddr*>(&addr),
            sizeof(addr)
        );
    }
    
    // 接收单个数据包
    ssize_t recvfrom(
        uint8_t* buffer,
        std::size_t buffer_size,
        char* ip_out = nullptr,
        uint16_t* port_out = nullptr
    ) {
        if (fd_ < 0) {
            return -1;
        }
        
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        
        ssize_t len = ::recvfrom(
            fd_,
            buffer,
            buffer_size,
            0,
            reinterpret_cast<struct sockaddr*>(&addr),
            &addr_len
        );
        
        if (len < 0) {
            return len;
        }
        
        if (ip_out) {
            strcpy(ip_out, inet_ntoa(addr.sin_addr));
        }
        if (port_out) {
            *port_out = ntohs(addr.sin_port);
        }
        
        return len;
    }
    
    // 批量接收到 ring buffer
    BatchIOResult recv_batch(PacketRing& ring, std::size_t max_packets) {
        if (fd_ < 0) {
            return {0, EBADF, BatchIOStatus::SysError};
        }
        
        BatchRecv recv(fd_);
        return recv.recv_batch(ring, max_packets);
    }

    UnifiedIOResult recv_batch_unified(
        PacketRing& ring,
        std::size_t max_packets,
        const BatchIOOptions& options = BatchIOOptions{}
    ) {
        if (fd_ < 0) {
            return {0, EBADF, UnifiedIOStatus::SysError};
        }
        BatchRecv recv(fd_);
        return to_unified_result(recv.recv_batch(ring, max_packets, options));
    }
    
    // 批量发送
    BatchIOResult send_batch(std::span<const Packet> packets) {
        if (fd_ < 0) {
            return {0, EBADF, BatchIOStatus::SysError};
        }
        
        BatchSend send(fd_);
        return send.send_batch(packets);
    }

    UnifiedIOResult send_batch_unified(
        std::span<const Packet> packets,
        const BatchIOOptions& options = BatchIOOptions{}
    ) {
        if (fd_ < 0) {
            return {0, EBADF, UnifiedIOStatus::SysError};
        }
        BatchSend send(fd_);
        return to_unified_result(send.send_batch(packets, options));
    }
    
    // 关闭 socket
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
            bound_ = false;
        }
    }
    
    // 获取文件描述符
    int fd() const noexcept {
        return fd_;
    }
    
    // 检查是否已绑定
    bool is_bound() const noexcept {
        return bound_;
    }

private:
    int fd_;
    bool bound_;
};

}  // namespace usn
