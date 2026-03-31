// SPDX-License-Identifier: MIT
//
// TCP 协议栈基础测试

#include <usn/core/memory_pool.hpp>
#include <usn/protocol/tcp_protocol.hpp>
#include <usn/protocol/tcp_state_machine.hpp>
#include <usn/protocol/tcp_reassembly.hpp>
#include <usn/protocol/tcp_retransmission.hpp>
#include <usn/protocol/tcp_congestion.hpp>
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

TEST_F(TcpProtocolTest, StateTransitionValidation) {
    TcpConnection conn;
    conn.state = TcpState::CLOSED;
    EXPECT_TRUE(TcpStateMachine::transition_state(conn, TcpState::LISTEN));
    EXPECT_FALSE(TcpStateMachine::transition_state(conn, TcpState::ESTABLISHED));
    EXPECT_EQ(conn.state, TcpState::LISTEN);
}

TEST_F(TcpProtocolTest, StateTransitionRejectMatrix) {
    TcpConnection c1;
    c1.state = TcpState::LISTEN;
    EXPECT_FALSE(TcpStateMachine::transition_state(c1, TcpState::FIN_WAIT_1));
    EXPECT_EQ(c1.state, TcpState::LISTEN);

    TcpConnection c2;
    c2.state = TcpState::ESTABLISHED;
    EXPECT_FALSE(TcpStateMachine::transition_state(c2, TcpState::SYN_SENT));
    EXPECT_EQ(c2.state, TcpState::ESTABLISHED);

    TcpConnection c3;
    c3.state = TcpState::TIME_WAIT;
    EXPECT_FALSE(TcpStateMachine::transition_state(c3, TcpState::ESTABLISHED));
    EXPECT_EQ(c3.state, TcpState::TIME_WAIT);
}

TEST_F(TcpProtocolTest, HandleIncomingHandshakeAndAck) {
    TcpConnection server;
    server.state = TcpState::LISTEN;
    server.local_port = 9000;
    server.remote_port = 9101;
    server.local_ip = inet_addr("127.0.0.1");
    server.remote_ip = inet_addr("127.0.0.1");

    Packet syn = TcpProtocol::create_syn(9101, 9000, server.remote_ip, server.local_ip, 10, pool.allocate());
    TcpHeader syn_header{};
    const uint8_t* payload = nullptr;
    std::size_t payload_len = 0;
    ASSERT_TRUE(TcpProtocol::parse(syn, syn_header, payload, payload_len));
    ASSERT_TRUE(TcpStateMachine::handle_incoming(server, syn_header, payload_len));
    EXPECT_EQ(server.state, TcpState::SYN_RECEIVED);
    EXPECT_EQ(server.send_ack, 11u);

    Packet ack = TcpProtocol::create_ack(server, server.send_ack, pool.allocate());
    TcpHeader ack_header{};
    ASSERT_TRUE(TcpProtocol::parse(ack, ack_header, payload, payload_len));
    ASSERT_TRUE(TcpStateMachine::handle_incoming(server, ack_header, payload_len));
    EXPECT_EQ(server.state, TcpState::ESTABLISHED);

    pool.deallocate(syn.data);
    pool.deallocate(ack.data);
}

TEST_F(TcpProtocolTest, ReassemblyBufferOutOfOrder) {
    TcpReassemblyBuffer buf;
    const uint8_t seg2[] = {4, 5, 6};
    const uint8_t seg1[] = {1, 2, 3};
    ASSERT_TRUE(buf.insert(4, seg2, sizeof(seg2)));
    ASSERT_TRUE(buf.insert(1, seg1, sizeof(seg1)));
    EXPECT_EQ(buf.segment_count(), 2u);

    std::vector<TcpBufferedSegment> out;
    std::size_t consumed = buf.pop_contiguous(1, out);
    EXPECT_EQ(consumed, 6u);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].seq, 1u);
    EXPECT_EQ(out[1].seq, 4u);
}

TEST_F(TcpProtocolTest, ReassemblyBufferHoleTracking) {
    TcpReassemblyBuffer buf;
    const uint8_t seg_a[] = {7, 8};       // seq 7-9
    const uint8_t seg_b[] = {12, 13, 14}; // seq 12-15
    ASSERT_TRUE(buf.insert(7, seg_a, sizeof(seg_a)));
    ASSERT_TRUE(buf.insert(12, seg_b, sizeof(seg_b)));

    const auto holes = buf.hole_ranges(1);
    ASSERT_EQ(holes.size(), 2u);
    EXPECT_EQ(holes[0].start_seq, 1u);
    EXPECT_EQ(holes[0].end_seq, 7u);
    EXPECT_EQ(holes[1].start_seq, 9u);
    EXPECT_EQ(holes[1].end_seq, 12u);
    EXPECT_EQ(buf.hole_count(1), 2u);
    EXPECT_EQ(buf.total_hole_bytes(1), 9u);
}

TEST_F(TcpProtocolTest, RetransmissionQueueAndAck) {
    TcpRetransmissionQueue q;
    q.track_segment(100, 20, 1000, 50);
    q.track_segment(120, 20, 1000, 50);
    EXPECT_EQ(q.size(), 2u);

    auto due = q.collect_due(1060, 50, 3);
    EXPECT_EQ(due.size(), 2u);
    EXPECT_EQ(q.ack_up_to(120), 1u);
    EXPECT_EQ(q.size(), 1u);
}

TEST_F(TcpProtocolTest, RetransmissionQueueMaxRetriesDrop) {
    TcpRetransmissionQueue q;
    q.track_segment(10, 10, 1000, 10);
    for (int i = 0; i < 4; ++i) {
        (void)q.collect_due(1015 + i * 20, 10, 2);
    }
    EXPECT_EQ(q.size(), 0u);
}

TEST_F(TcpProtocolTest, FlowControlCanSendAndWindowUpdate) {
    TcpConnection conn;
    conn.peer_window = 64;
    EXPECT_TRUE(TcpStateMachine::can_send(conn, 32));
    EXPECT_FALSE(TcpStateMachine::can_send(conn, 128));

    TcpStateMachine::on_window_update(conn, 2048);
    EXPECT_EQ(conn.peer_window, 2048u);
    EXPECT_EQ(conn.window_size, 2048u);
}

TEST_F(TcpProtocolTest, RtoEstimatorTracksRtt) {
    TcpRtoEstimator est;
    const uint64_t base = est.rto_ms();
    est.sample_rtt(20);
    EXPECT_GE(est.rto_ms(), 50u);
    est.sample_rtt(400);
    EXPECT_NE(est.rto_ms(), base);
}

TEST_F(TcpProtocolTest, ClosePathInitiateAndFinalAck) {
    TcpConnection conn;
    conn.state = TcpState::ESTABLISHED;
    EXPECT_TRUE(TcpStateMachine::initiate_close(conn));
    EXPECT_EQ(conn.state, TcpState::FIN_WAIT_1);

    TcpHeader ack{};
    ack.ack_num = 100;
    ack.window = 1024;
    ack.flags = (1 << 4);  // ACK
    ASSERT_TRUE(TcpStateMachine::handle_incoming(conn, ack, 0));
    EXPECT_EQ(conn.state, TcpState::FIN_WAIT_2);
}

TEST_F(TcpProtocolTest, PassiveCloseToLastAckAndClosed) {
    TcpConnection conn;
    conn.state = TcpState::ESTABLISHED;

    TcpHeader fin{};
    fin.seq_num = 7;
    fin.window = 1024;
    fin.flags = (1 << 0);  // FIN
    ASSERT_TRUE(TcpStateMachine::handle_incoming(conn, fin, 0));
    EXPECT_EQ(conn.state, TcpState::CLOSE_WAIT);

    EXPECT_TRUE(TcpStateMachine::initiate_close(conn));
    EXPECT_EQ(conn.state, TcpState::LAST_ACK);

    TcpHeader ack{};
    ack.ack_num = conn.send_ack;
    ack.window = 1024;
    ack.flags = (1 << 4);  // ACK
    ASSERT_TRUE(TcpStateMachine::handle_incoming(conn, ack, 0));
    EXPECT_EQ(conn.state, TcpState::CLOSED);
}

TEST_F(TcpProtocolTest, KeepaliveHelpers) {
    EXPECT_TRUE(TcpStateMachine::should_send_keepalive(2000, 1000, 500));
    EXPECT_FALSE(TcpStateMachine::should_send_keepalive(1200, 1000, 500));

    TcpConnection conn;
    conn.local_port = 1111;
    conn.remote_port = 2222;
    conn.local_ip = inet_addr("127.0.0.1");
    conn.remote_ip = inet_addr("127.0.0.1");
    conn.send_seq = 10;
    conn.send_ack = 20;

    Packet ka = TcpStateMachine::create_keepalive(conn, pool.allocate());
    TcpHeader h{};
    const uint8_t* payload = nullptr;
    std::size_t payload_len = 0;
    ASSERT_TRUE(TcpProtocol::parse(ka, h, payload, payload_len));
    EXPECT_TRUE(h.has_ack());
    EXPECT_FALSE(h.has_fin());
    EXPECT_EQ(payload_len, 0u);
    pool.deallocate(ka.data);
}

TEST_F(TcpProtocolTest, ChecksumValidationAndCorruption) {
    const uint32_t src_ip = inet_addr("127.0.0.1");
    const uint32_t dst_ip = inet_addr("127.0.0.1");
    Packet p = TcpProtocol::create_syn(12345, 80, src_ip, dst_ip, 1000, pool.allocate());

    p.data[16] = 0;
    p.data[17] = 0;
    const uint16_t sum = TcpProtocol::calculate_checksum(p.data, p.len, src_ip, dst_ip);
    p.data[16] = static_cast<uint8_t>(sum >> 8);
    p.data[17] = static_cast<uint8_t>(sum & 0xFF);
    EXPECT_TRUE(TcpProtocol::validate_checksum(p, src_ip, dst_ip));

    p.data[17] ^= 0x1;
    EXPECT_FALSE(TcpProtocol::validate_checksum(p, src_ip, dst_ip));
    pool.deallocate(p.data);
}

TEST_F(TcpProtocolTest, ParseAndValidateRejectsMalformedFlags) {
    uint32_t src_ip = inet_addr("192.168.1.1");
    uint32_t dst_ip = inet_addr("192.168.1.2");
    Packet syn = TcpProtocol::create_syn(12345, 80, src_ip, dst_ip, 1000, pool.allocate());

    syn.data[13] |= 0x01;
    syn.data[16] = 0;
    syn.data[17] = 0;
    const uint16_t fixed_checksum = TcpProtocol::calculate_checksum(syn.data, syn.len, src_ip, dst_ip);
    syn.data[16] = static_cast<uint8_t>(fixed_checksum >> 8);
    syn.data[17] = static_cast<uint8_t>(fixed_checksum & 0xFF);

    TcpHeader h{};
    const uint8_t* payload = nullptr;
    std::size_t payload_len = 0;
    EXPECT_FALSE(TcpProtocol::parse_and_validate(syn, src_ip, dst_ip, h, payload, payload_len));
    pool.deallocate(syn.data);
}

TEST_F(TcpProtocolTest, DuplicateAckAndDuplicatePayloadAreTolerated) {
    TcpConnection conn;
    conn.state = TcpState::ESTABLISHED;
    conn.recv_ack = 200;
    conn.recv_seq = 300;
    conn.send_ack = 300;

    TcpHeader dup_ack{};
    dup_ack.flags = (1 << 4);  // ACK
    dup_ack.ack_num = 200;
    dup_ack.window = 1024;
    EXPECT_TRUE(TcpStateMachine::handle_incoming(conn, dup_ack, 0));
    EXPECT_EQ(conn.state, TcpState::ESTABLISHED);
    EXPECT_EQ(conn.recv_ack, 200u);

    TcpHeader dup_seg{};
    dup_seg.flags = (1 << 4);  // ACK
    dup_seg.ack_num = 201;
    dup_seg.seq_num = 250;
    dup_seg.window = 1024;
    EXPECT_TRUE(TcpStateMachine::handle_incoming(conn, dup_seg, 16));
    EXPECT_EQ(conn.recv_seq, 300u);
}

TEST_F(TcpProtocolTest, CongestionControllerSlowStartAvoidanceAndLoss) {
    TcpCongestionController cc(1000);
    EXPECT_EQ(cc.cwnd_bytes(), 1000u);

    cc.on_ack(1000);
    EXPECT_EQ(cc.cwnd_bytes(), 2000u);
    cc.on_ack(1000);
    EXPECT_EQ(cc.cwnd_bytes(), 3000u);

    cc.on_ack(7000);
    EXPECT_GE(cc.cwnd_bytes(), 8000u);
    const auto before_ca = cc.cwnd_bytes();
    cc.on_ack(before_ca);
    EXPECT_GT(cc.cwnd_bytes(), before_ca);

    cc.on_loss();
    EXPECT_EQ(cc.cwnd_bytes(), 1000u);
    EXPECT_GE(cc.ssthresh_bytes(), 2000u);
}
