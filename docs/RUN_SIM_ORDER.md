# run_sim_order — 订单仿真

`run_sim_order` 拉起 **`order_gateway` + `order_client`**，模拟一条最小化的 **TCP 订单链路**：客户端按配置 QPS 发送订单请求，网关在单线程事件循环中处理并回复 ACK，客户端统计 RTT 分位与超时、重连等。

## 前置条件

| 项 | 说明 |
| --- | --- |
| 平台 | Linux（`epoll`、`fork`/`execv`、本地 TCP） |
| 构建 | 已编译出 `build/apps/order_gateway` 与 `build/apps/order_client` |
| 工作目录 | **须在仓库根目录执行**：编排器使用相对路径 `./build/apps/...` |
| 端口 | 网关监听 **`0.0.0.0:9000`**；客户端默认连 **`127.0.0.1:9000`**。内核侧源端口由 `connect()` **临时分配**；源码里 `kClientPort = 9101` 仅写入用户态 `TcpConnection` 供自研报文头使用，**不等于** OS 五元组里的本地端口。若 **9000** 已被占用，网关启动会失败 |
| 权限 | 一般无需 root |

相关集成测试（可选）：`tests/run_netns_suite.py`，见 [NETNS_TESTING.md](./NETNS_TESTING.md)。

## 仿真场景

| 角色 | 做什么 |
| --- | --- |
| 网关 (`order_gateway`) | 在 **TCP 9000** 上 listen（`0.0.0.0`）；`epoll` 驱动；解析订单并回 ACK；支持背压阈值、`server_drop_at_s` 等实验开关 |
| 客户端 (`order_client`) | 连接网关；按 `qps` 与 `burst` 发送；可混合 NEW/CANCEL/REPLACE；统计 RTT、超时、重连 |

整条链路是 **下单 → 网关确认 → 测延迟** 的请求–应答循环，用于验证 TCP 订单通路与指标。

## 订单消息内容

与 `usn::apps::OrderRequest` / `OrderAck` 一致（见 `include/usn/apps/messages.hpp`）。

| 字段 | 值 | 含义 |
| --- | --- | --- |
| client_order_id | 从 1 递增 | 客户端本地订单号 |
| target_order_id | NEW 为 0；CANCEL/REPLACE 为被引用订单 | 混合流量时由客户端生成 |
| instrument_id | 10001 | 模拟合约 ID |
| action | `NEW` / `CANCEL` / `REPLACE` | 由 `--cancel-ratio` / `--replace-ratio` 与剩余比例决定 |
| side | 交替买/卖 | 奇数单卖、偶数单买（实现细节以源码为准） |
| price | `100000 + (id % 100)`（REPLACE 时可能 `+1`） | 1e-4 精度下约 **10.0000–10.0099**（与源码注释一致） |
| quantity | 1 | 固定 |
| send_ts_ns | steady clock 纳秒 | 客户端打戳，用于统计 |

网关侧对 NEW/CANCEL/REPLACE 有 **最小订单表语义**，会产生 `reject_total` / `reject_rate` 等（用于实验，非真实撮合）。

## 命令行参数（编排器）

编排器源码：`apps/run_sim_order.cpp`。参数均为 **`--key value` 成对**；`--help` 打印用法并 **以退出码 0 退出**。

| 参数 | 默认值 | 合法范围 / 钳制 | 说明 |
| --- | --- | --- | --- |
| `--duration` | `10` | ≥ 1 | 网关与客户端拉起后的运行秒数 |
| `--qps` | `2` | ≥ 1 | 客户端目标每秒请求数（与 burst 配合） |
| `--burst` | `1` | ≥ 1 | 每个周期内连续发送的突发条数 |
| `--disconnect-interval-s` | `0` | ≥ 0 | 客户端周期性断开重连的间隔（秒）；`0` 表示不启用该逻辑 |
| `--timeout-ms` | `1000` | ≥ 1 | 客户端等待 ACK 的超时（毫秒） |
| `--cancel-ratio` | `0` | 0–100 | CANCEL 占比（与 NEW/REPLACE 分配相关，见客户端实现） |
| `--replace-ratio` | `0` | 0–100 | REPLACE 占比 |
| `--server-drop-at-s` | `0` | ≥ 0 | 传给网关：运行若干秒后主动丢弃/断开类实验（秒）；`0` 关闭 |
| `--gateway-backpressure-threshold` | `0` | ≥ 0 | 传给网关的 **`--backpressure-threshold`**；`0` 通常表示不启用该阈值 |

**映射说明**：编排器里的 `--gateway-backpressure-threshold` 会原样转为网关进程的 `--backpressure-threshold`（命名在两层 CLI 上不一致，以源码为准）。

单独调试时可直接运行 `build/apps/order_gateway` / `build/apps/order_client`（客户端另支持 `--gateway-ip` 等，编排器当前固定走本机子进程，未暴露该参数）。

## 运行示例

```bash
# 在仓库根目录、已 cmake 构建后
cmake -S . -B build && cmake --build build -j

# 默认：10s、qps=2、burst=1、timeout-ms=1000
./build/apps/run_sim_order

# 显式参数
./build/apps/run_sim_order --duration 30 --qps 200 --burst 4

# 进阶：周期断连、超时、混合动作、背压、服务端定时 drop
./build/apps/run_sim_order --duration 30 --qps 400 --burst 8 \
  --disconnect-interval-s 5 --timeout-ms 800 \
  --cancel-ratio 20 --replace-ratio 30 \
  --gateway-backpressure-threshold 16 --server-drop-at-s 0

./build/apps/run_sim_order --help
```

## 生命周期

1. 启动 `order_gateway`（传入 `--server-drop-at-s`、`--backpressure-threshold`）。
2. **等待 300 ms**。
3. 启动 `order_client`（传入 qps、burst、disconnect、timeout、cancel/replace 比例）。
4. 睡眠 `--duration` 秒。
5. 先 **SIGINT 客户端**，再 **SIGINT 网关**，回收子进程。

## 指标与日志

### 网关（每秒一行，前缀 `[order_gateway][metrics]`）

| 字段 | 含义 |
| --- | --- |
| `req_recv_total` | 累计收到请求数 |
| `ack_sent_total` | 累计发出 ACK 数 |
| `active_conn` / `active_conn_peak` | 当前连接数与历史峰值 |
| `accept_failures` | accept 失败次数 |
| `unexpected_disconnects` | 异常断开计数 |
| `reject_total` | 语义拒绝次数 |
| `reject_rate` | `reject_total / req_recv_total`（无请求时为 0） |
| `backpressure_hits` | 背压触发次数 |
| `reorder_buffered_total` | TCP 重排缓冲相关累计 |
| `reorder_replayed_bytes` | 重排后补投负载的字节累计 |
| `reorder_hole_events` / `reorder_hole_peak` / `reorder_hole_bytes_peak` | 序号空洞检测与峰值 |
| `keepalive_recv_total` | 收到 keepalive 相关段次数 |

### 客户端（每秒一行，前缀 `[order_client][metrics]`）

| 字段 | 含义 |
| --- | --- |
| `req_sent_total` / `ack_recv_total` | 请求与 ACK 累计 |
| `ack_timeout_total` | 等待 ACK 超时次数 |
| `reconnect_total` | 重连次数 |
| `send_blocked_total` | 发送路径阻塞/暂不可写累计 |
| `retransmit_total` / `retransmit_failed_total` / `retransmit_drop_total` | 重传相关计数 |
| `cwnd_blocked_total` / `congestion_loss_total` | 拥塞窗口阻塞与「丢包」信号累计 |
| `cwnd_bytes` / `ssthresh_bytes` / `rto_ms` | 拥塞控制与 RTO 快照 |
| `keepalive_sent_total` | 客户端发出 keepalive 累计 |
| `new_total` / `cancel_total` / `replace_total` | 各动作发送条数 |
| `rtt_p50_us` / `rtt_p99_us` / `rtt_p999_us` | 全体 RTT 分位（微秒） |
| `rtt_new_p99_us` / `rtt_cancel_p99_us` / `rtt_replace_p99_us` | 分动作 p99（样本为空时为 0） |

退出前客户端会再打一条 **`[order_client][final]`**：含 RTT 分位及重传/拥塞相关字段（**不含**按动作拆分的 p99；与每秒 metrics 行字段集合略有不同）。

**RTT 基准**：本机 loopback、低 QPS 时 RTT 常见 **几十到几百微秒**；QPS、burst、断连、网关背压或 `server_drop_at_s` 都会显著改变分布。

## 本地冒烟验收（建议）

在 **本机、中等以下 QPS、未刻意打开断连与服务端 drop** 的前提下：

| 检查项 | 建议标准 |
| --- | --- |
| 通路 | 运行期间 `ack_recv_total` 持续增长，且与 `req_sent_total` 大致匹配 |
| 超时 | `ack_timeout_total` 在健康 loopback 上宜为 **0**（或极低） |
| 拒绝 | `reject_total` 是否在可接受范围取决于你配置的 cancel/replace 比例与网关语义 |

若启用 `--disconnect-interval-s`、`--server-drop-at-s` 或极高负载，超时与重连上升属于预期，应结合分位 RTT 与网关 `backpressure_hits` 一并解读。

## 故障排查

| 现象 | 可能原因 | 处理方向 |
| --- | --- | --- |
| `connect` / 子进程立即退出 | 未在仓库根目录、二进制缺失、或 **9000 被占用** | 确认 `./build/apps/order_*` 存在；`ss -ltnp` / `lsof -i :9000` |
| 大量 `ack_timeout_total` | `timeout-ms` 过短、网关跟不上、或开启 drop/背压 | 增大 timeout；降 QPS；看网关 `backpressure_hits` |
| `reject_rate` 很高 | CANCEL/REPLACE 引用不存在的订单等语义 | 阅读客户端生成 target_id 的逻辑；调低 cancel/replace 比例做基线 |
| RTT 飙升 | CPU 饱和、`burst` 过大、或网关卡顿 | 对比单机空闲时的 p50；减少并发或 duration 内总压力 |

## 退出码

| 码 | 含义 |
| --- | --- |
| `0` | 正常结束（含 `--help`） |
| `1` | 未知参数、子进程启动失败等 |

## 另见

- [RUN_SIM_MARKET.md](./RUN_SIM_MARKET.md) — UDP 组播行情仿真
- [NETNS_TESTING.md](./NETNS_TESTING.md) — netns 集成测试
- [TCP_PROTOCOL.md](./TCP_PROTOCOL.md) — 用户态 TCP 相关说明（若仓库内存在）
