// SPDX-License-Identifier: MIT
//
// TCP 状态机转换、连接管理辅助函数

#pragma once

#include <usn/protocol/tcp_protocol.hpp>

#include <cstddef>
#include <cstdint>

namespace usn {

class TcpStateMachine {
public:
    static bool can_transition(TcpState from, TcpState to) {
        switch (from) {
            case TcpState::CLOSED:
                return to == TcpState::LISTEN || to == TcpState::SYN_SENT;
            case TcpState::LISTEN:
                return to == TcpState::SYN_RECEIVED || to == TcpState::CLOSED;
            case TcpState::SYN_SENT:
                return to == TcpState::ESTABLISHED || to == TcpState::CLOSED;
            case TcpState::SYN_RECEIVED:
                return to == TcpState::ESTABLISHED || to == TcpState::CLOSED;
            case TcpState::ESTABLISHED:
                return to == TcpState::FIN_WAIT_1 || to == TcpState::CLOSE_WAIT;
            case TcpState::FIN_WAIT_1:
                return to == TcpState::FIN_WAIT_2 || to == TcpState::CLOSING || to == TcpState::TIME_WAIT;
            case TcpState::FIN_WAIT_2:
                return to == TcpState::TIME_WAIT;
            case TcpState::CLOSE_WAIT:
                return to == TcpState::LAST_ACK;
            case TcpState::CLOSING:
                return to == TcpState::TIME_WAIT;
            case TcpState::LAST_ACK:
                return to == TcpState::CLOSED;
            case TcpState::TIME_WAIT:
                return to == TcpState::CLOSED;
        }
        return false;
    }

    static bool transition_state(TcpConnection& conn, TcpState to) {
        if (!can_transition(conn.state, to)) {
            return false;
        }
        conn.state = to;
        return true;
    }

    static bool initiate_close(TcpConnection& conn) {
        if (conn.state == TcpState::ESTABLISHED) {
            return transition_state(conn, TcpState::FIN_WAIT_1);
        }
        if (conn.state == TcpState::CLOSE_WAIT) {
            return transition_state(conn, TcpState::LAST_ACK);
        }
        return false;
    }

    static bool should_send_keepalive(uint64_t now_ms, uint64_t last_activity_ms, uint64_t interval_ms) {
        return now_ms > last_activity_ms && (now_ms - last_activity_ms) >= interval_ms;
    }

    static Packet create_keepalive(
        const TcpConnection& conn,
        uint8_t* out_buffer
    ) {
        return TcpProtocol::create_ack(conn, conn.send_ack, out_buffer);
    }

    static bool can_send(const TcpConnection& conn, std::size_t payload_len) {
        return payload_len <= conn.peer_window;
    }

    static void on_window_update(TcpConnection& conn, uint16_t window) {
        conn.peer_window = window;
        conn.window_size = window;
    }

    static bool handle_incoming(
        TcpConnection& conn,
        const TcpHeader& header,
        std::size_t payload_len
    ) {
        on_window_update(conn, header.window);

        if (header.has_syn()) {
            conn.recv_seq = header.seq_num + 1;
            if (conn.state == TcpState::LISTEN) {
                conn.send_ack = conn.recv_seq;
                return transition_state(conn, TcpState::SYN_RECEIVED);
            }
            if (conn.state == TcpState::SYN_SENT && header.has_ack()) {
                conn.send_ack = header.ack_num;
                return transition_state(conn, TcpState::ESTABLISHED);
            }
        }

        if (header.has_ack()) {
            if (header.ack_num <= conn.recv_ack && !header.has_fin()) {
                return true;
            }
            conn.recv_ack = header.ack_num;
            if (conn.state == TcpState::SYN_RECEIVED) {
                return transition_state(conn, TcpState::ESTABLISHED);
            }
            if (conn.state == TcpState::FIN_WAIT_1) {
                if (header.has_fin()) {
                    return transition_state(conn, TcpState::TIME_WAIT);
                }
                return transition_state(conn, TcpState::FIN_WAIT_2);
            }
            if (conn.state == TcpState::CLOSING) {
                return transition_state(conn, TcpState::TIME_WAIT);
            }
            if (conn.state == TcpState::LAST_ACK) {
                return transition_state(conn, TcpState::CLOSED);
            }
        }

        if (payload_len > 0 && header.seq_num == conn.recv_seq) {
            conn.recv_seq += static_cast<uint32_t>(payload_len);
            conn.send_ack = conn.recv_seq;
        } else if (payload_len > 0 && header.seq_num < conn.recv_seq) {
            return true;
        }

        if (header.has_fin()) {
            if (payload_len > 0 && header.seq_num + static_cast<uint32_t>(payload_len) == conn.recv_seq) {
                conn.recv_seq += 1;
            } else {
                conn.recv_seq = header.seq_num + static_cast<uint32_t>(payload_len) + 1;
            }
            conn.send_ack = conn.recv_seq;
            if (conn.state == TcpState::ESTABLISHED) {
                return transition_state(conn, TcpState::CLOSE_WAIT);
            }
            if (conn.state == TcpState::FIN_WAIT_1) {
                return transition_state(conn, TcpState::CLOSING);
            }
            if (conn.state == TcpState::FIN_WAIT_2 || conn.state == TcpState::FIN_WAIT_1) {
                return transition_state(conn, TcpState::TIME_WAIT);
            }
        }

        return true;
    }
};

}  // namespace usn
