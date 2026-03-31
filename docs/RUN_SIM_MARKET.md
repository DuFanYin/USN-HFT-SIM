# run_sim_market — 行情仿真

`run_sim_market` 模拟一个最小化的 **UDP 组播行情链路**：一个发布端持续向组播地址广播行情快照，一个订阅端实时接收并检测序号连续性。

## 仿真场景

| 角色 | 做什么 |
| --- | --- |
| 发布端 (feed_publisher) | 按 `rate` 向组播组 `239.0.0.1:9100` 发送行情消息，消息内携带递增序号和发送时间戳 |
| 订阅端 (feed_subscriber) | 加入同一组播组，接收后做序号连续性检查与延迟统计，周期性输出 p50/p99/max |

整个仿真就是一条 **"发布 → 组播 → 接收"** 的单向数据流，用来验证 UDP 行情通道是否工作正常。

## 行情消息内容

每条行情包含以下字段（固定值，不做随机化）：

| 字段 | 值 | 含义 |
| --- | --- | --- |
| seq | 从 1 递增 | 全局序号，供订阅端做连续性校验 |
| stream_id | `seq % streams` | 模拟多流标识 |
| instrument_id | 10001 | 模拟合约 ID |
| bid_price | 100000 | 买一价（1e-4 精度，即 10.0000） |
| bid_qty | 10 | 买一量 |
| ask_price | 100010 | 卖一价（10.0010） |
| ask_qty | 10 | 卖一量 |
| send_ts_ns | steady clock 纳秒时间戳 | 发送时刻，用于计算端到端延迟 |

## 运行方式

```bash
# 默认运行 10 秒，默认速率 2 msg/s，默认 payload 64 bytes
./build/apps/run_sim_market --duration 10 --rate 2 --payload-size 64

# 显式指定参数（推荐）
./build/apps/run_sim_market --duration 30 --rate 50 --payload-size 128

# 进阶场景（V2/V3）
./build/apps/run_sim_market --duration 30 --rate 200 --payload-size 128 --subscribers 3 --streams 2 --jitter-ms 2 --drop-rate 0.01 --reorder-rate 0.02 --slow-subscriber-ratio 33

# 查看完整参数说明
./build/apps/run_sim_market --help
```

## 生命周期

1. **启动订阅端** — 先让订阅端就绪，确保不会错过最早的行情
2. **等待 300 ms** — 给订阅端留出加入组播组的时间
3. **启动发布端** — 开始广播行情
4. **运行 N 秒** — 发布端按速率发送，订阅端持续接收并输出指标
5. **停止** — 先停发布端，再停订阅端，全部回收后退出

## 观察要点

- **正常情况**：订阅端持续收到行情，序号无跳号，按秒输出指标
- **GAP 告警**：如果网络丢包或订阅端启动晚于发布端，会出现序号跳跃的 GAP 日志
- **发送频率**：由 `--rate` 决定，发送间隔约 `1/rate` 秒
- **payload 大小**：由 `--payload-size` 决定，最小不小于协议头字段长度
- **注入参数**：`--drop-rate` / `--reorder-rate` / `--jitter-ms`
- **多实例参数**：`--subscribers` / `--streams` / `--slow-subscriber-ratio`

## 指标输出（V1）

发布端每秒输出：

- `sent_total`
- `rate`
- `payload_size`
- `dropped_total`
- `reordered_total`
- `streams`

订阅端每秒输出：

- `recv_total`
- `gaps`
- `latency_p50_us`
- `latency_p99_us`
- `latency_p999_us`
- `latency_max_us`
- `reorder_events`
- `max_reorder_depth`

退出时订阅端会再打印一条 `[final]` 汇总指标。

主进程还会输出一条 `[run_market_sim][summary]` 汇总（总接收、总 gap、最差延迟及对应订阅者）。

## 退出码

- `0`：正常结束
- `1`：参数错误或子进程启动失败等异常退出
