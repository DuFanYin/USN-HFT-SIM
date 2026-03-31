# run_sim_order — 订单仿真

`run_sim_order` 模拟一个最小化的 **TCP 订单链路**：一个客户端持续向网关提交订单，网关收到后打印订单内容并回复确认，客户端测量往返延迟。

## 仿真场景

| 角色 | 做什么 |
| --- | --- |
| 网关 (order_gateway) | 在 TCP 端口 9000 上监听，接受客户端连接；接收请求并回复 ACK，同时输出接收/回包指标 |
| 客户端 (order_client) | 连接网关，按 `qps + burst` 发送订单请求；等待 ACK 并统计 RTT 分位与超时计数 |

整个仿真就是一条 **"下单 → 网关确认 → 测延迟"** 的请求-应答循环，用来验证 TCP 订单通道是否工作正常。

## 订单消息内容

每笔订单的字段如下：

| 字段 | 值 | 含义 |
| --- | --- | --- |
| client_order_id | 从 1 递增 | 客户端本地订单号 |
| target_order_id | 对 NEW 为 0；对 CANCEL/REPLACE 为目标订单号 | 混合请求时引用的原订单 |
| instrument_id | 10001 | 模拟合约 ID |
| action | `NEW/CANCEL/REPLACE` | 订单动作类型 |
| side | 交替买/卖 | 奇数单卖出，偶数单买入 |
| price | 100000 + (id % 100) | 模拟价格微小波动（范围 1000.00–1000.99） |
| quantity | 1 | 固定数量 |
| send_ts_ns | steady clock 纳秒时间戳 | 发送时刻，用于请求时间标记 |

> 注意：网关当前实现了最小订单表语义校验（NEW/CANCEL/REPLACE），用于输出 `reject_total/reject_rate` 等实验指标。

## 运行方式

```bash
# 默认运行 10 秒，默认 qps=2，burst=1
./build/apps/run_sim_order --duration 10 --qps 2 --burst 1

# 显式指定参数（推荐）
./build/apps/run_sim_order --duration 30 --qps 200 --burst 4

# 进阶场景（V2/V3）
./build/apps/run_sim_order --duration 30 --qps 400 --burst 8 --disconnect-interval-s 5 --timeout-ms 800 --cancel-ratio 20 --replace-ratio 30 --gateway-backpressure-threshold 16 --server-drop-at-s 0

# 查看完整参数说明
./build/apps/run_sim_order --help
```

## 生命周期

1. **启动网关** — 先让网关开始监听端口
2. **等待 300 ms** — 给网关留出绑定和监听的时间
3. **启动客户端** — 连接网关，开始发送订单
4. **运行 N 秒** — 客户端按节奏发送请求，网关持续接收和确认
5. **停止** — 先停客户端，再停网关，全部回收后退出

## 观察要点

- **网关侧指标**：按秒输出 `req_recv_total/ack_sent_total/active_conn`
- **客户端侧指标**：按秒输出 `req_sent_total/ack_recv_total/ack_timeout_total/rtt_p50_us/rtt_p99_us/rtt_p999_us`
- **RTT 基准**：本机 loopback 下 RTT 通常在几十到几百微秒
- **发送频率**：由 `qps` 和 `burst` 共同决定，单个 burst 内连续发送
- **连接控制**：`--disconnect-interval-s` / `--timeout-ms` / `--server-drop-at-s`
- **混合请求/背压**：`--cancel-ratio` / `--replace-ratio` / `--gateway-backpressure-threshold`

## 指标输出（V1）

网关每秒输出：

- `req_recv_total`
- `ack_sent_total`
- `active_conn`
- `active_conn_peak`
- `accept_failures`
- `unexpected_disconnects`
- `reject_rate`
- `backpressure_hits`
- `reject_total`

客户端每秒输出：

- `req_sent_total`
- `ack_recv_total`
- `ack_timeout_total`
- `reconnect_total`
- `rtt_p50_us`
- `rtt_p99_us`
- `rtt_p999_us`
- `rtt_new_p99_us`
- `rtt_cancel_p99_us`
- `rtt_replace_p99_us`

退出时客户端会再打印一条 `[final]` 汇总指标。

## 退出码

- `0`：正常结束
- `1`：参数错误或子进程启动失败等异常退出
