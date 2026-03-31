// SPDX-License-Identifier: MIT
//
// Lightweight tracing utilities for simulation apps.

#pragma once

#include <cstdint>
#include <iostream>
#include <string_view>

namespace usn {

struct TraceConfig {
    bool enabled{false};
    uint32_t sample_every{1};
};

class Tracer {
public:
    explicit Tracer(TraceConfig config = {})
        : config_(config) {}

    bool should_emit() {
        ++counter_;
        if (!config_.enabled) {
            return false;
        }
        if (config_.sample_every == 0) {
            return false;
        }
        return (counter_ % config_.sample_every) == 0;
    }

    void emit(
        std::string_view component,
        std::string_view event,
        uint64_t packet_id,
        uint64_t ts_ns,
        uint64_t queue_depth,
        uint64_t latency_us = 0
    ) {
        if (!should_emit()) {
            return;
        }
        std::cout << "[trace]"
                  << " component=" << component
                  << " event=" << event
                  << " packet_id=" << packet_id
                  << " ts_ns=" << ts_ns
                  << " queue_depth=" << queue_depth
                  << " latency_us=" << latency_us
                  << "\n";
    }

private:
    TraceConfig config_{};
    uint64_t counter_{0};
};

}  // namespace usn
