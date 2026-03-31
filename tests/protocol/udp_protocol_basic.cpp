// SPDX-License-Identifier: MIT
//
// UDP 协议栈基础测试

#include <usn/core/memory_pool.hpp>
#include <usn/protocol/udp_protocol.hpp>
#include <gtest/gtest.h>
#include <array>
#include <cstring>

using namespace usn;

class UdpProtocolTest : public ::testing::Test {
protected:
    MemoryPool pool{256, 16};
};

TEST_F(UdpProtocolTest, Encapsulate) {
    const char* payload = "Hello, UDP!";
    std::size_t payload_len = std::strlen(payload);

    std::array<uint8_t, 256> buffer{};

    Packet packet = UdpProtocol::encapsulate(
        reinterpret_cast<const uint8_t*>(payload),
        payload_len, 12345, 54321, 0, 0, false,
        buffer.data()
    );

    EXPECT_EQ(packet.len, sizeof(UdpHeader) + payload_len);

    UdpHeader header;
    const uint8_t* parsed_payload;
    std::size_t parsed_len;

    ASSERT_TRUE(UdpProtocol::parse(packet, header, parsed_payload, parsed_len));
    EXPECT_EQ(header.source_port, 12345);
    EXPECT_EQ(header.dest_port, 54321);
    EXPECT_EQ(parsed_len, payload_len);
    EXPECT_EQ(std::memcmp(parsed_payload, payload, payload_len), 0);
}

TEST_F(UdpProtocolTest, Parse) {
    const char* payload = "Test payload";
    std::array<uint8_t, 256> buffer{};

    Packet packet = UdpProtocol::encapsulate(
        reinterpret_cast<const uint8_t*>(payload),
        std::strlen(payload), 1000, 2000, 0, 0, false,
        buffer.data()
    );

    UdpHeader header;
    const uint8_t* parsed_payload;
    std::size_t parsed_len;

    ASSERT_TRUE(UdpProtocol::parse(packet, header, parsed_payload, parsed_len));
    EXPECT_EQ(header.source_port, 1000);
    EXPECT_EQ(header.dest_port, 2000);
}

TEST_F(UdpProtocolTest, Checksum) {
    const char* payload = "Checksum test";
    uint32_t src_ip = inet_addr("192.168.1.1");
    uint32_t dst_ip = inet_addr("192.168.1.2");

    std::array<uint8_t, 256> buffer{};

    Packet packet = UdpProtocol::encapsulate(
        reinterpret_cast<const uint8_t*>(payload),
        std::strlen(payload), 1234, 5678,
        src_ip, dst_ip, true,
        buffer.data()
    );

    EXPECT_TRUE(UdpProtocol::verify_checksum(packet, src_ip, dst_ip));
}
