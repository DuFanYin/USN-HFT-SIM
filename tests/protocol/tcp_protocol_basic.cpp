// SPDX-License-Identifier: MIT
//
// TCP 协议栈基础测试

#include <usn/core/memory_pool.hpp>
#include <usn/protocol/tcp_protocol.hpp>
#include <gtest/gtest.h>
#include <cstring>

using namespace usn;

class TcpProtocolTest : public ::testing::Test {
protected:
    MemoryPool pool{256, 16};
};

TEST_F(TcpProtocolTest, Syn) {
    uint32_t src_ip = inet_addr("192.168.1.1");
    uint32_t dst_ip = inet_addr("192.168.1.2");

    Packet syn = TcpProtocol::create_syn(12345, 80, src_ip, dst_ip, 1000, pool.allocate());

    EXPECT_EQ(syn.len, TcpProtocol::kHeaderLen);

    TcpHeader header;
    const uint8_t* payload;
    std::size_t payload_len;

    ASSERT_TRUE(TcpProtocol::parse(syn, header, payload, payload_len));
    EXPECT_TRUE(header.has_syn());
    EXPECT_FALSE(header.has_ack());
    EXPECT_EQ(header.source_port, 12345);
    EXPECT_EQ(header.dest_port, 80);

    pool.deallocate(syn.data);
}

TEST_F(TcpProtocolTest, Data) {
    TcpConnection conn;
    conn.local_ip = inet_addr("192.168.1.1");
    conn.remote_ip = inet_addr("192.168.1.2");
    conn.local_port = 12345;
    conn.remote_port = 80;
    conn.send_seq = 1000;
    conn.recv_seq = 2000;

    const char* data = "Hello, TCP!";
    Packet packet = TcpProtocol::create_data(
        conn,
        reinterpret_cast<const uint8_t*>(data),
        std::strlen(data),
        pool.allocate()
    );

    TcpHeader header;
    const uint8_t* payload;
    std::size_t payload_len;

    ASSERT_TRUE(TcpProtocol::parse(packet, header, payload, payload_len));
    EXPECT_TRUE(header.has_ack());
    EXPECT_GT(payload_len, 0u);

    pool.deallocate(packet.data);
}
