// SPDX-License-Identifier: MIT
//
// UDP delivery feedback helper (gap/reorder observability).

#pragma once

#include <algorithm>
#include <cstdint>

namespace usn {

struct UdpDeliveryFeedback {
    uint64_t expected_seq{1};
    uint64_t total{0};
    uint64_t gaps{0};
    uint64_t reorder_events{0};
    uint64_t max_reorder_depth{0};

    void observe(uint64_t seq) {
        ++total;
        if (seq == expected_seq) {
            ++expected_seq;
            return;
        }
        if (seq > expected_seq) {
            gaps += (seq - expected_seq);
            expected_seq = seq + 1;
            return;
        }
        ++reorder_events;
        max_reorder_depth = std::max(max_reorder_depth, expected_seq - seq);
    }
};

}  // namespace usn
