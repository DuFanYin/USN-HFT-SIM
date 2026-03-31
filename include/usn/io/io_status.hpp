// SPDX-License-Identifier: MIT
//
// Unified IO status conversions across backends.
// For pure types without backend deps, include <usn/io/io_status_types.hpp>.

#pragma once

#include <usn/io/io_status_types.hpp>
#include <usn/io/batch_io.hpp>
#include <usn/io/epoll_wrapper.hpp>
#include <usn/io/io_uring_wrapper.hpp>
#include <usn/io/busy_poll.hpp>

namespace usn {

inline constexpr UnifiedIOStatus to_unified_status(BatchIOStatus status) noexcept {
    switch (status) {
        case BatchIOStatus::Ok:
            return UnifiedIOStatus::Ok;
        case BatchIOStatus::Timeout:
            return UnifiedIOStatus::Timeout;
        case BatchIOStatus::Cancelled:
            return UnifiedIOStatus::Cancelled;
        case BatchIOStatus::WouldBlock:
            return UnifiedIOStatus::WouldBlock;
        case BatchIOStatus::SysError:
            return UnifiedIOStatus::SysError;
    }
    return UnifiedIOStatus::SysError;
}

inline constexpr UnifiedIOStatus to_unified_status(EpollWaitStatus status) noexcept {
    switch (status) {
        case EpollWaitStatus::Ok:
            return UnifiedIOStatus::Ok;
        case EpollWaitStatus::Timeout:
            return UnifiedIOStatus::Timeout;
        case EpollWaitStatus::Cancelled:
            return UnifiedIOStatus::Cancelled;
        case EpollWaitStatus::SysError:
            return UnifiedIOStatus::SysError;
    }
    return UnifiedIOStatus::SysError;
}

inline constexpr UnifiedIOStatus to_unified_status(IoUringWaitStatus status) noexcept {
    switch (status) {
        case IoUringWaitStatus::Ok:
            return UnifiedIOStatus::Ok;
        case IoUringWaitStatus::Timeout:
            return UnifiedIOStatus::Timeout;
        case IoUringWaitStatus::Cancelled:
            return UnifiedIOStatus::Cancelled;
        case IoUringWaitStatus::NotInitialized:
            return UnifiedIOStatus::NotInitialized;
        case IoUringWaitStatus::SysError:
            return UnifiedIOStatus::SysError;
    }
    return UnifiedIOStatus::SysError;
}

inline constexpr UnifiedIOStatus to_unified_status(BusyPollStatus status) noexcept {
    switch (status) {
        case BusyPollStatus::Completed:
            return UnifiedIOStatus::Ok;
        case BusyPollStatus::Timeout:
            return UnifiedIOStatus::Timeout;
        case BusyPollStatus::Cancelled:
            return UnifiedIOStatus::Cancelled;
        case BusyPollStatus::Stopped:
            return UnifiedIOStatus::Stopped;
    }
    return UnifiedIOStatus::SysError;
}

inline constexpr UnifiedIOResult to_unified_result(const BatchIOResult& result) noexcept {
    return {result.count, result.error, to_unified_status(result.status)};
}

inline constexpr UnifiedIOResult to_unified_result(const EpollWaitResult& result) noexcept {
    return {static_cast<std::size_t>(result.count > 0 ? result.count : 0), result.error, to_unified_status(result.status)};
}

inline constexpr UnifiedIOResult to_unified_result(const IoUringWaitResult& result) noexcept {
    return {static_cast<std::size_t>(result.count > 0 ? result.count : 0), result.error, to_unified_status(result.status)};
}

inline constexpr UnifiedIOResult to_unified_result(const BusyPollResult& result) noexcept {
    return {static_cast<std::size_t>(result.data_hits), 0, to_unified_status(result.status)};
}

}  // namespace usn
