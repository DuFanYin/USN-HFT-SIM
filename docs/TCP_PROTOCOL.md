# TCP 协议支持说明（当前实现）

本文基于 `include/usn/protocol/tcp_*.hpp` 与现有测试用例，记录本项目当前 **已经实现** 的 TCP 能力与边界。

## 1. 覆盖的模块

- `tcp_protocol.hpp`：TCP 报文头定义、封包/解包、校验和、基础合法性检查
- `tcp_state_machine.hpp`：连接状态机、收包驱动状态迁移、窗口更新、keepalive 辅助
- `tcp_retransmission.hpp`：重传队列、超时回收、RTO 估算
- `tcp_reassembly.hpp`：乱序重组缓冲、hole 统计
- `tcp_congestion.hpp`：简化拥塞控制（slow start + congestion avoidance）
- `tcp_socket.hpp`：基于系统 socket 的 TCP 封装（server/client、accept、read/write）

## 2. 已实现能力（按功能域）

### 2.1 TCP 报文构造与解析

已实现：
- TCP 固定头（20B）结构定义与网络/主机字节序转换
- 报文构造：
  - `create_syn`（三次握手第 1 步）
  - `create_syn_ack`（三次握手第 2 步）
  - `create_ack`
  - `create_data`（ACK + PSH，带 payload）
  - `create_fin`（FIN + ACK）
- 报文解析 `parse`：可提取 `TcpHeader`、payload 指针及长度
- 基础头部畸形判断 `is_malformed_header`：
  - `data_offset < 5`
  - `SYN` 与 `FIN` 同时置位
- 一体化解析校验 `parse_and_validate`

当前约束：
- 仅处理最小 TCP 头；不解析/生成 TCP options（MSS/SACK/Timestamp/Window Scale 等）
- 标志位仅提供 `SYN/ACK/FIN/RST` 常用判定接口

### 2.2 校验和

已实现：
- TCP 伪头部校验和计算 `calculate_checksum`（IPv4）
- 报文校验 `validate_checksum`
- 支持在构造报文时自动写入 checksum

当前约束：
- 设计和实现面向 IPv4（伪头部字段按 IPv4 处理）

### 2.3 连接状态机与生命周期

已实现状态：
- `CLOSED`
- `LISTEN`
- `SYN_SENT`
- `SYN_RECEIVED`
- `ESTABLISHED`
- `FIN_WAIT_1`
- `FIN_WAIT_2`
- `CLOSE_WAIT`
- `CLOSING`
- `LAST_ACK`
- `TIME_WAIT`

已实现能力：
- 状态迁移校验 `can_transition`
- 执行迁移 `transition_state`
- 主动关闭入口 `initiate_close`：
  - `ESTABLISHED -> FIN_WAIT_1`
  - `CLOSE_WAIT -> LAST_ACK`
- 基于入站头部的处理 `handle_incoming`：
  - SYN 握手推进（含 `LISTEN -> SYN_RECEIVED`、`SYN_SENT -> ESTABLISHED`）
  - ACK 驱动状态推进（含 `SYN_RECEIVED/FIN_WAIT_1/CLOSING/LAST_ACK` 相关路径）
  - payload 顺序接收推进 `recv_seq/send_ack`
  - 重复 ACK、重复 payload 的容忍处理
  - FIN 触发被动关闭/半关闭相关迁移
- keepalive 辅助：
  - `should_send_keepalive`
  - `create_keepalive`（本质为 ACK 包）

当前约束：
- 未实现 TIME_WAIT 定时器、2MSL 管理
- 未实现完整 RFC 级异常路径（例如大量 RST 场景处理、挑战 ACK、防护逻辑）
- 状态机为“教学/仿真简化版”，非内核级完备 TCP

### 2.4 流量控制与窗口

已实现：
- `TcpConnection` 内维护本端窗口与对端窗口（`window_size/recv_window/peer_window`）
- `on_window_update` 同步更新窗口
- 发送前窗口检查 `can_send(payload_len <= peer_window)`

当前约束：
- 未实现 window scaling
- 未实现 persist timer / zero-window probe
- 发送控制为简化检查，不含完整发送队列调度

### 2.5 重传与 RTO

已实现：
- 发送段追踪 `track_segment(seq_start, payload_len, now_ms, rto_ms)`
- ACK 回收 `ack_up_to(ack_num)`
- 超时收集 `collect_due`：
  - 支持最大重试次数
  - 超限自动移除
  - 指数退避（简化）
- RTO 估算 `TcpRtoEstimator`：
  - SRTT/RTTVAR 更新
  - RTO 上下限裁剪（50ms ~ 5000ms）

当前约束：
- 未实现 Karn 算法/重传样本过滤等更精细 RTT 采样策略
- 未实现 fast retransmit / fast recovery（基于 dupACK 的快速重传）

### 2.6 乱序重组

已实现：
- 乱序段插入 `insert(seq, data, len)`
- 按期望序号提取连续数据 `pop_contiguous`
- hole 可观测性：
  - `hole_ranges`
  - `hole_count`
  - `total_hole_bytes`

当前约束：
- 未实现 segment overlap 合并策略（同起点重复插入直接拒绝）
- 未内置最大缓存水位控制策略（由上层决定）

### 2.7 拥塞控制（简化）

已实现：
- `cwnd/ssthresh` 维护
- `can_send(inflight, payload, peer_window)`：受拥塞窗口与接收窗口共同限制
- `on_ack`：
  - 慢启动阶段按 ACK 字节增长
  - 拥塞避免阶段累计近似“每 RTT 增 1 MSS”
- `on_loss`：
  - `ssthresh = max(2*MSS, cwnd/2)`
  - `cwnd` 回到 `1 MSS`

当前约束：
- 未实现 Reno/CUBIC/BBR 等具体算法细节
- 未实现 ECN、pacing、hybrid slow start

### 2.8 TCP Socket 封装

已实现：
- 服务端创建 `create_server(port, backlog, nonblock)`（`SO_REUSEADDR`、`TCP_NODELAY`）
- 客户端连接 `connect_to(ip, port, timeout_ms)`：
  - 非阻塞 connect + poll 超时
  - 完成后恢复阻塞并设置 `SO_RCVTIMEO`
- `accept/accept_from`（支持 nonblock）
- `read/write` 与静态 `read_from/write_to`
- RAII `close/release/fd/is_open`

当前约束：
- 未封装 TLS
- 未提供 send/recv buffer、keepalive 等更多 sockopt 配置接口

## 3. 测试已覆盖点（摘要）

来自 `tests/protocol/tcp_protocol_basic.cpp`、`tests/protocol/tcp_socket_basic.cpp`：
- SYN/Data 包构造解析
- checksum 校验与损坏检测
- parse_and_validate 对畸形标志拒绝
- 握手与关闭关键路径状态迁移
- 重复 ACK/重复 payload 容忍
- 流控窗口更新与发送判断
- 重传队列、重试上限淘汰、RTO 基本更新
- 乱序重组与 hole 统计
- 拥塞控制增长/丢包回退
- socket 基础创建、连接失败路径

## 4. 定位建议

如果要继续增强 TCP 能力，建议优先顺序：
- TCP options（MSS/WS/SACK）
- fast retransmit + fast recovery
- 更完整的关闭/异常路径与 timer（TIME_WAIT/persist）
- 更真实的发送队列与 in-flight 管理
