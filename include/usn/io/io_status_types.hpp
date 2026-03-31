// SPDX-License-Identifier: MIT
//
// Unified IO status types — no backend dependencies.

#pragma once

#include <cstddef>

namespace usn {

enum class UnifiedIOStatus {
    Ok,
    Timeout,
    Cancelled,
    WouldBlock,
    NotInitialized,
    Stopped,
    SysError
};

struct UnifiedIOResult {
    std::size_t count{0};
    int error{0};
    UnifiedIOStatus status{UnifiedIOStatus::Ok};

    bool success() const noexcept {
        return status != UnifiedIOStatus::SysError && status != UnifiedIOStatus::NotInitialized;
    }
};

enum class LoopControl {
    ContinueWork,
    ContinueIdle,
    Stop
};

inline constexpr bool is_retryable_idle_status(UnifiedIOStatus status) noexcept {
    return status == UnifiedIOStatus::Timeout || status == UnifiedIOStatus::WouldBlock;
}

inline constexpr bool is_terminal_error_status(UnifiedIOStatus status) noexcept {
    return status == UnifiedIOStatus::SysError || status == UnifiedIOStatus::NotInitialized;
}

inline constexpr bool should_continue_loop(const UnifiedIOResult& result) noexcept {
    return result.status == UnifiedIOStatus::Ok || is_retryable_idle_status(result.status);
}

inline constexpr LoopControl classify_loop_control(const UnifiedIOResult& result) noexcept {
    if (result.status == UnifiedIOStatus::Ok) {
        return LoopControl::ContinueWork;
    }
    if (is_retryable_idle_status(result.status)) {
        return LoopControl::ContinueIdle;
    }
    return LoopControl::Stop;
}

}  // namespace usn
