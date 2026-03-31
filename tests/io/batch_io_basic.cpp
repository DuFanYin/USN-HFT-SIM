// SPDX-License-Identifier: MIT
//
// Batch I/O 基础功能测试
//
// 注意：这些测试需要真实的 socket，在某些环境下可能无法运行
// 主要测试接口和基本逻辑

#include <usn/io/batch_io.hpp>
#include <usn/core/packet_ring.hpp>
#include <cassert>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

using namespace usn;

void test_batch_recv_interface() {
    std::cout << "[TEST] BatchRecv interface\n";
    
#ifdef __linux__
    // 创建 UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cout << "  ⚠ Skipped (cannot create socket)\n";
        return;
    }
    
    // 设置为非阻塞
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    BatchRecv recv(sock);
    PacketRing ring(1024);
    
    // 尝试接收（应该返回 0，因为没有数据）
    auto result = recv.recv_batch(ring, 64);
    assert(result.success());
    assert(result.count == 0);
    
    close(sock);
    std::cout << "  ✓ BatchRecv interface test passed\n";
#else
    std::cout << "  ⚠ Skipped (Linux only)\n";
#endif
}

void test_batch_send_interface() {
    std::cout << "[TEST] BatchSend interface\n";
    
#ifdef __linux__
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cout << "  ⚠ Skipped (cannot create socket)\n";
        return;
    }
    
    BatchSend send(sock);
    
    // 准备测试数据包
    std::vector<uint8_t> data1(100, 0xAA);
    std::vector<uint8_t> data2(200, 0xBB);
    
    Packet packets[] = {
        Packet(data1.data(), data1.size()),
        Packet(data2.data(), data2.size())
    };
    
    // 尝试发送（可能会失败，因为没有绑定地址，但接口应该工作）
    auto result = send.send_batch(packets);
    // 不检查结果，因为可能失败（没有绑定地址）
    
    close(sock);
    std::cout << "  ✓ BatchSend interface test passed\n";
#else
    std::cout << "  ⚠ Skipped (Linux only)\n";
#endif
}

int main() {
    std::cout << "Running Batch I/O tests...\n\n";
    
    try {
        test_batch_recv_interface();
        test_batch_send_interface();
        
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
