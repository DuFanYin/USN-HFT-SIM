// SPDX-License-Identifier: MIT
//
// TCP 乱序重组缓冲区

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace usn {

struct TcpBufferedSegment {
    uint32_t seq{0};
    std::vector<uint8_t> data;
};

struct TcpHoleRange {
    uint32_t start_seq{0};
    uint32_t end_seq{0};
};

class TcpReassemblyBuffer {
public:
    bool insert(uint32_t seq, const uint8_t* data, std::size_t len) {
        if (data == nullptr || len == 0) {
            return false;
        }
        auto [it, inserted] = segments_.emplace(seq, std::vector<uint8_t>(data, data + len));
        if (!inserted) {
            return false;
        }
        total_bytes_ += len;
        return true;
    }

    std::size_t pop_contiguous(uint32_t expected_seq, std::vector<TcpBufferedSegment>& out) {
        out.clear();
        std::size_t consumed = 0;
        uint32_t cursor = expected_seq;
        while (true) {
            auto it = segments_.find(cursor);
            if (it == segments_.end()) {
                break;
            }
            TcpBufferedSegment seg{};
            seg.seq = it->first;
            seg.data = std::move(it->second);
            cursor += static_cast<uint32_t>(seg.data.size());
            consumed += seg.data.size();
            out.push_back(std::move(seg));
            segments_.erase(it);
        }
        total_bytes_ = (consumed > total_bytes_) ? 0 : (total_bytes_ - consumed);
        return consumed;
    }

    std::size_t size() const noexcept { return total_bytes_; }
    std::size_t segment_count() const noexcept { return segments_.size(); }

    std::vector<TcpHoleRange> hole_ranges(uint32_t expected_seq, std::size_t max_ranges = 8) const {
        std::vector<TcpHoleRange> holes;
        holes.reserve(max_ranges);
        uint32_t cursor = expected_seq;
        for (const auto& [seq, data] : segments_) {
            if (holes.size() >= max_ranges) {
                break;
            }
            if (seq > cursor) {
                holes.push_back(TcpHoleRange{cursor, seq});
            }
            const uint32_t seg_end = seq + static_cast<uint32_t>(data.size());
            if (seg_end > cursor) {
                cursor = seg_end;
            }
        }
        return holes;
    }

    std::size_t hole_count(uint32_t expected_seq) const {
        std::size_t holes = 0;
        uint32_t cursor = expected_seq;
        for (const auto& [seq, data] : segments_) {
            if (seq > cursor) {
                ++holes;
            }
            const uint32_t seg_end = seq + static_cast<uint32_t>(data.size());
            if (seg_end > cursor) {
                cursor = seg_end;
            }
        }
        return holes;
    }

    uint32_t total_hole_bytes(uint32_t expected_seq) const {
        uint32_t total = 0;
        uint32_t cursor = expected_seq;
        for (const auto& [seq, data] : segments_) {
            if (seq > cursor) {
                total += (seq - cursor);
            }
            const uint32_t seg_end = seq + static_cast<uint32_t>(data.size());
            if (seg_end > cursor) {
                cursor = seg_end;
            }
        }
        return total;
    }

private:
    std::map<uint32_t, std::vector<uint8_t>> segments_;
    std::size_t total_bytes_{0};
};

}  // namespace usn
