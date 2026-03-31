// SPDX-License-Identifier: MIT
//
// Busy Polling 实现
//
// 设计目标：
// - 忙轮询模式（避免上下文切换）
// - CPU 占用控制
// - 自适应轮询策略

#pragma once

#include <usn/core/packet_ring.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include <functional>

namespace usn {

// Busy Polling 配置
struct BusyPollConfig {
    std::chrono::nanoseconds poll_interval{1000};  // 轮询间隔（纳秒）
    std::chrono::nanoseconds max_idle_time{10000};  // 最大空闲时间
    unsigned max_iterations{1000};                 // 最大迭代次数
    bool adaptive{true};                           // 自适应模式
};

// Busy Polling 类
class BusyPoller {
public:
    explicit BusyPoller(const BusyPollConfig& config = BusyPollConfig())
        : config_(config)
        , running_(false)
        , idle_count_(0) {
    }
    
    // 开始忙轮询
    void start(
        std::function<bool()> poll_func,
        std::function<void()> on_idle = nullptr
    ) {
        running_.store(true);
        
        auto start_time = std::chrono::steady_clock::now();
        unsigned iterations = 0;
        
        while (running_.load(std::memory_order_acquire)) {
            bool has_data = poll_func();
            
            if (has_data) {
                idle_count_.store(0, std::memory_order_relaxed);
                iterations = 0;
            } else {
                idle_count_.fetch_add(1, std::memory_order_relaxed);
                iterations++;
                
                // 检查是否需要 yield
                if (iterations >= config_.max_iterations) {
                    std::this_thread::yield();
                    iterations = 0;
                }
                
                // 检查是否空闲时间过长
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - start_time
                );
                
                if (elapsed >= config_.max_idle_time) {
                    if (on_idle) {
                        on_idle();
                    }
                    // 短暂睡眠
                    std::this_thread::sleep_for(config_.poll_interval);
                    start_time = std::chrono::steady_clock::now();
                } else {
                    // 忙等待（spin）
                    if (config_.poll_interval.count() > 0) {
                        std::this_thread::sleep_for(config_.poll_interval);
                    }
                }
            }
        }
    }
    
    // 停止忙轮询
    void stop() {
        running_.store(false, std::memory_order_release);
    }
    
    // 检查是否运行中
    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }
    
    // 获取空闲计数
    unsigned idle_count() const noexcept {
        return idle_count_.load(std::memory_order_relaxed);
    }
    
    // 自适应调整轮询间隔
    void adaptive_adjust() {
        if (!config_.adaptive) {
            return;
        }
        
        unsigned idle = idle_count();
        
        if (idle > 1000) {
            // 空闲时间长，增加轮询间隔
            config_.poll_interval = std::min(
                config_.poll_interval * 2,
                std::chrono::nanoseconds(1000000)  // 最大 1ms
            );
        } else if (idle < 100) {
            // 空闲时间短，减少轮询间隔
            config_.poll_interval = std::max(
                config_.poll_interval / 2,
                std::chrono::nanoseconds(100)  // 最小 100ns
            );
        }
    }

private:
    BusyPollConfig config_;
    std::atomic<bool> running_;
    std::atomic<unsigned> idle_count_;
};

}  // namespace usn
