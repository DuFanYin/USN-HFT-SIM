# UDP 协议支持说明（当前实现）

本文基于 `include/usn/protocol/udp_*.hpp` 与现有测试用例，记录本项目当前 **已经实现** 的 UDP 能力与边界。

## 1. 覆盖的模块

- `udp_protocol.hpp`：UDP 头定义、封包/解包、校验和计算与校验
- `udp_socket.hpp`：UDP socket 封装（单包收发、批量收发、统一状态返回）
- `udp_feedback.hpp`：UDP 投递反馈指标（gap/reorder 可观测）

## 2. 已实现能力（按功能域）

### 2.1 UDP 报文构造与解析

已实现：
- UDP 头 `UdpHeader`（8B）定义，含字节序转换
- `encapsulate`：
  - 构造 UDP 头（源/目的端口、长度、checksum）
  - 写入 payload
  - 可选 checksum 计算（由 `calculate_checksum_flag` 控制）
- `parse`：
  - 解析头部
  - 校验 `length` 合法性
  - 输出 payload 指针与长度

当前约束：
- 只覆盖 UDP 基础头，不涉及任何扩展头/隧道协议封装

### 2.2 校验和

已实现：
- `calculate_checksum`：IPv4 伪头部 + UDP 头 + payload
- `verify_checksum`：对完整报文进行校验，结果为 `calculated == 0`
- 支持在 `encapsulate` 中按需自动写 checksum

当前约束：
- 仅面向 IPv4 伪头部逻辑
- `encapsulate` 在 `src_ip/dst_ip == 0` 或关闭标志时可不写 checksum（用于某些测试/仿真场景）

### 2.3 UDP Socket 封装

已实现：
- `create`：创建 `AF_INET/SOCK_DGRAM` 并设置非阻塞
- `bind(port, ip)`：支持绑定指定 IP 或 `INADDR_ANY`
- `sendto(data, len, ip, port)`：单包发送
- `recvfrom(buffer, size, ip_out, port_out)`：单包接收并可回填来源地址
- `close/fd/is_bound`：生命周期与状态管理

当前约束：
- 仅封装 IPv4 地址字符串接口（`inet_aton`）
- 未提供 multicast/broadcast、DSCP/TOS、SO_REUSEPORT 等高级选项

### 2.4 批量 I/O 集成

已实现：
- `recv_batch(PacketRing&, max_packets)`：批量收包到 ring buffer
- `send_batch(std::span<const Packet>)`：批量发包
- `recv_batch_unified/send_batch_unified`：统一返回 `UnifiedIOResult`，便于上层统一处理错误与状态
- 与 `usn::BatchRecv/BatchSend`、`PacketRing` 集成

当前约束：
- 批量发送依赖传入 `Packet` 自身携带可发送目标信息（由上层组包阶段保证）
- 本层主要做薄封装，不承担复杂重试策略

### 2.5 UDP 投递反馈观测

`UdpDeliveryFeedback` 已实现以下可观测指标：
- `total`：已观测包总数
- `gaps`：累计缺失序号数（跳序向前时累计）
- `reorder_events`：乱序事件次数（收到小于 `expected_seq` 的包）
- `max_reorder_depth`：最大乱序深度
- `expected_seq`：当前期望下一个序号

行为特点：
- 按序到达：仅推进 `expected_seq`
- 跳跃到达：记录 gap 并推进期望序号
- 回退到达：计入 reorder

## 3. 测试已覆盖点（摘要）

来自 `tests/protocol/udp_protocol_basic.cpp`、`tests/protocol/udp_socket_basic.cpp`、`tests/protocol/udp_feedback_basic.cpp`：
- UDP 封包后解析字段正确（端口、长度、payload）
- checksum 计算与校验通过
- socket 在非法 fd 下统一错误返回
- feedback 对顺序/缺口/乱序统计行为正确

## 4. 当前定位与后续增强建议

当前 UDP 模块定位是“高性能实验基础件”：
- 已具备基础报文处理、批量 I/O 接口与可观测性指标
- 适合行情/广播型场景的上层协议开发

建议后续按需求增强：
- IPv6 支持
- multicast/broadcast 管理
- 发送侧 pacing/限速
- 更细粒度丢包与时延统计（例如 EWMA/分位数）
