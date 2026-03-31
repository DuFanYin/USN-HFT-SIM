// SPDX-License-Identifier: MIT
//
// 简化的 TCP 拥塞控制器（slow start + congestion avoidance）

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace usn {

class TcpCongestionController {
public:
    explicit TcpCongestionController(uint32_t mss_bytes = 1200)
        : mss_bytes_(mss_bytes == 0 ? 1200 : mss_bytes)
        , cwnd_bytes_(mss_bytes_)
        , ssthresh_bytes_(8 * mss_bytes_) {}

    bool can_send(std::size_t inflight_bytes, std::size_t payload_len, uint32_t peer_window) const {
        const std::size_t allowed = std::min<std::size_t>(cwnd_bytes_, peer_window);
        return inflight_bytes + payload_len <= allowed;
    }

    void on_ack(std::size_t acked_bytes) {
        if (acked_bytes == 0) {
            return;
        }
        if (cwnd_bytes_ < ssthresh_bytes_) {
            // Slow start: cwnd grows roughly by acked bytes.
            cwnd_bytes_ += static_cast<uint32_t>(acked_bytes);
        } else {
            // Congestion avoidance: about +1 MSS per cwnd bytes ACKed.
            ca_accumulator_bytes_ += static_cast<uint32_t>(acked_bytes);
            while (ca_accumulator_bytes_ >= cwnd_bytes_) {
                ca_accumulator_bytes_ -= cwnd_bytes_;
                cwnd_bytes_ += mss_bytes_;
            }
        }
        if (cwnd_bytes_ > max_cwnd_bytes_) {
            cwnd_bytes_ = max_cwnd_bytes_;
        }
    }

    void on_loss() {
        ssthresh_bytes_ = std::max<uint32_t>(2 * mss_bytes_, cwnd_bytes_ / 2);
        cwnd_bytes_ = mss_bytes_;
        ca_accumulator_bytes_ = 0;
    }

    uint32_t cwnd_bytes() const noexcept { return cwnd_bytes_; }
    uint32_t ssthresh_bytes() const noexcept { return ssthresh_bytes_; }

private:
    uint32_t mss_bytes_{1200};
    uint32_t cwnd_bytes_{1200};
    uint32_t ssthresh_bytes_{9600};
    uint32_t ca_accumulator_bytes_{0};
    static constexpr uint32_t max_cwnd_bytes_ = 1u << 20;
};

}  // namespace usn
