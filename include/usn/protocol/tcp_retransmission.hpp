// SPDX-License-Identifier: MIT
//
// TCP 重传队列与 RTO 估算器

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace usn {

struct TcpRetransmissionEntry {
    uint32_t seq_start{0};
    uint32_t seq_end{0};
    uint64_t deadline_ms{0};
    uint32_t retries{0};
};

class TcpRetransmissionQueue {
public:
    void track_segment(uint32_t seq_start, std::size_t payload_len, uint64_t now_ms, uint64_t rto_ms) {
        TcpRetransmissionEntry e{};
        e.seq_start = seq_start;
        e.seq_end = seq_start + static_cast<uint32_t>(payload_len);
        e.deadline_ms = now_ms + rto_ms;
        e.retries = 0;
        entries_.push_back(e);
    }

    std::size_t ack_up_to(uint32_t ack_num) {
        std::size_t removed = 0;
        while (!entries_.empty() && entries_.front().seq_end <= ack_num) {
            entries_.pop_front();
            ++removed;
        }
        return removed;
    }

    std::vector<TcpRetransmissionEntry> collect_due(uint64_t now_ms, uint64_t base_rto_ms, uint32_t max_retries = 8) {
        std::vector<TcpRetransmissionEntry> due;
        for (auto it = entries_.begin(); it != entries_.end();) {
            auto& entry = *it;
            if (entry.deadline_ms <= now_ms) {
                if (entry.retries >= max_retries) {
                    it = entries_.erase(it);
                    continue;
                }
                due.push_back(entry);
                ++entry.retries;
                // 简化指数退避
                uint64_t backoff = base_rto_ms << (entry.retries > 8 ? 8 : entry.retries);
                entry.deadline_ms = now_ms + backoff;
            }
            ++it;
        }
        return due;
    }

    std::size_t size() const noexcept { return entries_.size(); }

    bool contains(uint32_t seq_start) const noexcept {
        for (const auto& e : entries_) {
            if (e.seq_start == seq_start) {
                return true;
            }
        }
        return false;
    }

private:
    std::deque<TcpRetransmissionEntry> entries_;
};

class TcpRtoEstimator {
public:
    void sample_rtt(uint64_t rtt_ms) {
        if (rtt_ms == 0) {
            rtt_ms = 1;
        }
        if (!initialized_) {
            srtt_ms_ = rtt_ms;
            rttvar_ms_ = rtt_ms / 2;
            initialized_ = true;
        } else {
            const uint64_t delta = (srtt_ms_ > rtt_ms) ? (srtt_ms_ - rtt_ms) : (rtt_ms - srtt_ms_);
            rttvar_ms_ = (3 * rttvar_ms_ + delta) / 4;
            srtt_ms_ = (7 * srtt_ms_ + rtt_ms) / 8;
        }
        uint64_t candidate = srtt_ms_ + std::max<uint64_t>(1, 4 * rttvar_ms_);
        if (candidate < min_rto_ms_) candidate = min_rto_ms_;
        if (candidate > max_rto_ms_) candidate = max_rto_ms_;
        rto_ms_ = candidate;
    }

    uint64_t rto_ms() const noexcept { return rto_ms_; }

private:
    bool initialized_{false};
    uint64_t srtt_ms_{200};
    uint64_t rttvar_ms_{100};
    uint64_t rto_ms_{300};
    static constexpr uint64_t min_rto_ms_ = 50;
    static constexpr uint64_t max_rto_ms_ = 5000;
};

}  // namespace usn
