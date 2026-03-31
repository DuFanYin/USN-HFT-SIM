// SPDX-License-Identifier: MIT
//
// UDP 协议栈基础测试

#include <usn/protocol/udp_protocol.hpp>
#include <cassert>
#include <iostream>
#include <cstring>

using namespace usn;

void test_udp_encapsulate() {
    std::cout << "[TEST] UDP Encapsulate\n";
    
    const char* payload = "Hello, UDP!";
    std::size_t payload_len = std::strlen(payload);
    
    Packet packet = UdpProtocol::encapsulate(
        reinterpret_cast<const uint8_t*>(payload),
        payload_len,
        12345,  // src port
        54321,  // dst port
        0, 0,   // IP addresses (skip checksum)
        false   // don't calculate checksum
    );
    
    assert(packet.len == sizeof(UdpHeader) + payload_len);
    
    // 解析验证
    UdpHeader header;
    const uint8_t* parsed_payload;
    std::size_t parsed_len;
    
    bool success = UdpProtocol::parse(packet, header, parsed_payload, parsed_len);
    assert(success);
    assert(header.source_port == 12345);
    assert(header.dest_port == 54321);
    assert(parsed_len == payload_len);
    assert(std::memcmp(parsed_payload, payload, payload_len) == 0);
    
    delete[] packet.data;
    std::cout << "  ✓ UDP Encapsulate test passed\n";
}

void test_udp_parse() {
    std::cout << "[TEST] UDP Parse\n";
    
    // 创建 UDP 数据包
    const char* payload = "Test payload";
    Packet packet = UdpProtocol::encapsulate(
        reinterpret_cast<const uint8_t*>(payload),
        std::strlen(payload),
        1000,
        2000,
        0, 0,
        false
    );
    
    // 解析
    UdpHeader header;
    const uint8_t* parsed_payload;
    std::size_t parsed_len;
    
    bool success = UdpProtocol::parse(packet, header, parsed_payload, parsed_len);
    assert(success);
    assert(header.source_port == 1000);
    assert(header.dest_port == 2000);
    
    delete[] packet.data;
    std::cout << "  ✓ UDP Parse test passed\n";
}

void test_udp_checksum() {
    std::cout << "[TEST] UDP Checksum\n";
    
    const char* payload = "Checksum test";
    uint32_t src_ip = inet_addr("192.168.1.1");
    uint32_t dst_ip = inet_addr("192.168.1.2");
    
    Packet packet = UdpProtocol::encapsulate(
        reinterpret_cast<const uint8_t*>(payload),
        std::strlen(payload),
        1234,
        5678,
        src_ip,
        dst_ip,
        true  // calculate checksum
    );
    
    // 验证校验和
    bool valid = UdpProtocol::verify_checksum(packet, src_ip, dst_ip);
    assert(valid);
    
    delete[] packet.data;
    std::cout << "  ✓ UDP Checksum test passed\n";
}

int main() {
    std::cout << "Running UDP Protocol tests...\n\n";
    
    try {
        test_udp_encapsulate();
        test_udp_parse();
        test_udp_checksum();
        
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
