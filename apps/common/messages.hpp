// SPDX-License-Identifier: MIT
//
// 公共消息定义：订单系统 & 行情系统都会用到的基础类型

#pragma once

#include <cstdint>

namespace usn::apps {

// 简化的订单方向
enum class Side : uint8_t {
    Buy  = 1,
    Sell = 2,
};

enum class OrderAction : uint8_t {
    New = 1,
    Cancel = 2,
    Replace = 3,
};

// TCP 订单请求（客户端 -> 网关）
struct OrderRequest {
    uint64_t client_order_id;  // 客户端本地订单 ID
    uint64_t target_order_id;  // Cancel/Replace 目标订单 ID
    uint32_t instrument_id;    // 合约/股票 ID
    OrderAction action;        // 请求动作
    Side     side;             // 买/卖
    uint32_t price;            // 价格（例如 1e-4 精度）
    uint32_t quantity;         // 数量
    uint64_t send_ts_ns;       // 客户端发送时间戳（steady clock）
};

// TCP 订单应答（网关 -> 客户端）
struct OrderAck {
    uint64_t client_order_id;
    uint64_t server_order_id;
    bool     accepted;         // 是否被接受
};

// UDP 行情增量消息（publisher -> subscriber）
struct MarketDataIncrement {
    uint64_t seq;              // 行情序列号（全局）
    uint32_t stream_id;        // 模拟流 ID
    uint32_t instrument_id;
    uint32_t bid_price;
    uint32_t bid_qty;
    uint32_t ask_price;
    uint32_t ask_qty;
    uint64_t send_ts_ns;       // 发布时刻时间戳（steady clock）
};

}  // namespace usn::apps

