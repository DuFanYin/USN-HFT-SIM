// SPDX-License-Identifier: MIT
//
// Epoll 基础测试

#include <usn/io/epoll_wrapper.hpp>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <cassert>

using namespace usn;

void test_epoll_create() {
    std::cout << "[TEST] Epoll create\n";
    
    EpollWrapper epoll;
    assert(epoll.fd() >= 0);
    
    std::cout << "  ✓ Epoll create test passed\n";
}

void test_epoll_add_remove() {
    std::cout << "[TEST] Epoll add/remove\n";
    
    EpollWrapper epoll;
    
    // 创建测试 socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sock >= 0);
    
    // 添加到 epoll
    bool added = epoll.add(sock, EPOLLIN | EPOLLET);
    assert(added);
    
    // 从 epoll 移除
    bool removed = epoll.remove(sock);
    assert(removed);
    
    close(sock);
    std::cout << "  ✓ Epoll add/remove test passed\n";
}

int main() {
    std::cout << "Running Epoll tests...\n\n";
    
    try {
        test_epoll_create();
        test_epoll_add_remove();
        
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
