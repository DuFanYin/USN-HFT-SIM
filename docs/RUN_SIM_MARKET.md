# run_sim_market — 行情仿真

`run_sim_market` 拉起 **多个 `feed_subscriber` + 一个 `feed_publisher`**，模拟一条最小化的 **UDP 组播行情链路**：发布端向固定组播地址发送带序号与时间戳的快照类增量消息，订阅端接收并做序号连续性、乱序与延迟统计。

## 前置条件

| 项 | 说明 |
| --- | --- |
| 平台 | Linux（组播、`fork`/`execv`、`SIGINT` 停进程） |
| 构建 | 已编译出 `build/apps/feed_subscriber` 与 `build/apps/feed_publisher`（见仓库根目录 `README.md`） |
| 工作目录 | **须在仓库根目录执行**：编排器使用相对路径 `./build/apps/...` |
| 网络 | 默认组播 `239.0.0.1:9100`；本机 loopback 通常即可，若环境禁组播或路由异常需自行排查 |
| 权限 | 一般无需 root；与组播/TTL 相关的系统策略因环境而异 |

相关集成测试（可选）：在 Linux + root 下可用 `tests/run_netns_suite.py` 做带 `tc netem` 的回归，见 [NETNS_TESTING.md](./NETNS_TESTING.md)。

## 仿真场景

| 角色 | 做什么 |
| --- | --- |
| 发布端 (`feed_publisher`) | 按 `--rate` 向组播 `239.0.0.1:9100` 发送消息；可注入抖动、丢包、乱序（由发布端逻辑模拟） |
| 订阅端 (`feed_subscriber`) | 绑定同端口并 `IP_ADD_MEMBERSHIP`；批量接收、统计 gap、延迟分位与乱序深度；编排器为每个订阅者写入 `/tmp/usn_run_market_sub_<父pid>_<id>.txt` 供汇总 |

整条链路是 **发布 → 组播 → 接收** 的单向数据流，用于验证 UDP 行情通道与观测指标是否合理。

## 行情消息内容

每条消息对应 `usn::apps::MarketDataIncrement`（字段布局见 `include/usn/apps/messages.hpp`）。文档表格中的数值与发布端填充一致：

| 字段 | 值 | 含义 |
| --- | --- | --- |
| seq | 从 1 递增 | 全局序号，供连续性校验 |
| stream_id | `(seq - 1) % streams` | 多流标识（与 `--streams` 一致） |
| instrument_id | 10001 | 模拟合约 ID |
| bid_price | 100000 | 买一价（1e-4 精度，即 10.0000） |
| bid_qty | 10 | 买一量 |
| ask_price | 100010 | 卖一价（10.0010） |
| ask_qty | 10 | 卖一量 |
| send_ts_ns | steady clock 纳秒 | 发送时刻，用于端到端延迟 |

## 命令行参数（编排器）

编排器源码：`apps/run_sim_market.cpp`。参数均为 **`--key value` 成对** 形式；`--help` 打印用法并 **以退出码 0 退出**。

| 参数 | 默认值 | 合法范围 / 钳制 | 说明 |
| --- | --- | --- | --- |
| `--duration` | `10` | ≥ 1 | 订阅者与发布端拉起后的运行秒数 |
| `--rate` | `2` | ≥ 1 | 发布速率（条/秒）；间隔约 `1/rate` 秒 |
| `--payload-size` | 见下文 | 指定时 orchestrator 侧 ≥ 64 | 传给 `feed_publisher` 的 payload 字节数意向值 |
| `--subscribers` | `1` | ≥ 1 | 订阅进程个数 |
| `--streams` | `1` | ≥ 1 | 传给发布端的并行流数 |
| `--jitter-ms` | `0` | ≥ 0 | 发送前随机 sleep 上界（毫秒） |
| `--drop-rate` | `0.0` | 0.0–1.0 | 发布端随机丢弃概率 |
| `--reorder-rate` | `0.0` | 0.0–1.0 | 发布端乱序概率（需队列内至少 2 条） |
| `--slow-subscriber-ratio` | `0` | 0–100 | 订阅者 `id` 满足 `(id * 100 / subscribers) < ratio` 时使用 `--artificial-delay-ms 2`，否则为 0（与「恰好 ratio%」略有离散误差） |

**默认 payload 说明（易踩坑）**：编排器未指定 `--payload-size` 时，内部初值为 `2*sizeof(uint64_t) + 5*sizeof(uint32_t)`（36）。`feed_publisher` 会把有效 payload **抬升到至少** `sizeof(MarketDataIncrement)`（当前为 **40** 字节）。一旦你在命令行指定 `--payload-size`，编排器会先 **`max(64, 你给的值)`** 再交给发布端；发布端仍会保证不小于消息体结构大小。

## 运行示例

```bash
# 在仓库根目录、已 cmake 构建后
cmake -S . -B build && cmake --build build -j

# 默认：10s、2 msg/s、单订阅者（payload 实际至少 40 字节）
./build/apps/run_sim_market

# 显式参数
./build/apps/run_sim_market --duration 30 --rate 50 --payload-size 128

# 进阶：多订阅、多流、注入抖动/丢包/乱序、部分“慢订阅者”
./build/apps/run_sim_market --duration 30 --rate 200 --payload-size 128 \
  --subscribers 3 --streams 2 --jitter-ms 2 \
  --drop-rate 0.01 --reorder-rate 0.02 --slow-subscriber-ratio 33

./build/apps/run_sim_market --help
```

单独调试组件时，可直接运行 `build/apps/feed_subscriber` / `build/apps/feed_publisher`（二者自有参数，与编排器传递的不完全一致，以 `--help` 或源码为准）。

## 生命周期

1. 按 `--subscribers` 启动多个 `feed_subscriber`（带 `--subscriber-id`、`--summary-file`；按 `slow-subscriber-ratio` 决定是否加 `--artificial-delay-ms 2`）。
2. **等待 300 ms**，便于订阅端完成 bind / 加组。
3. 启动 `feed_publisher`，传入 rate、payload、streams、jitter、drop、reorder。
4. 睡眠 `--duration` 秒。
5. 先 **SIGINT 发布端**，再依次 **SIGINT 各订阅端**，`waitpid` 回收。
6. 父进程读取各 `/tmp/usn_run_market_sub_*.txt` 首行，打印 `[run_market_sim][summary]`。

## 指标与日志

### 发布端（每秒一行，前缀 `[feed_publisher][metrics]`）

| 字段 | 含义 |
| --- | --- |
| `sent_total` | 累计发送条数 |
| `rate` | 目标发送速率（条/秒） |
| `payload_size` | 实际使用的 payload 字节数（不小于 `MarketDataIncrement`） |
| `streams` | 流数 |
| `dropped_total` | 因 `drop-rate` 丢弃的条数 |
| `reordered_total` | 乱序发送路径累计次数 |
| `drop_rate` / `reorder_rate` | 当前配置的注入概率 |

### 订阅端（每秒一行，前缀 `[feed_subscriber][metrics]`）

| 字段 | 含义 |
| --- | --- |
| `recv_total` | 累计接收条数 |
| `gaps` | 序号不连续次数（丢包、晚加入、发布端丢弃等均可导致） |
| `latency_p50_us` / `latency_p99_us` / `latency_p999_us` / `latency_max_us` | 端到端延迟（微秒）分位 |
| `reorder_events` | 检测到的乱序事件数 |
| `max_reorder_depth` | 观测到的最大乱序深度 |

退出前订阅端会再打一条 **`[final]`** 汇总。

父进程汇总行 **`[run_market_sim][summary]`** 含：`total_recv`、`total_gaps`、`total_reorder_events`、`worst_latency_max_us`、`worst_subscriber_id`（在能读到 summary 文件时）。

## 本地冒烟验收（建议）

在 **本机、低负载、无 `drop-rate`/`reorder-rate`** 的前提下，用于“链路通了”的自检：

| 检查项 | 建议标准 |
| --- | --- |
| 接收 | `total_recv > 0` |
| 序号 | `total_gaps == 0`（若发布端未丢弃且订阅先于发布稳定收包） |
| 延迟 | `latency_p99_us` 在 loopback 上通常远低于秒级；具体阈值依机器负载而定 |

一旦打开 `drop-rate` 或人为 `reorder-rate`，**gap / reorder 非零属于预期**，应改看分位延迟与业务可容忍丢包策略，而非强求 `gaps == 0`。

## 故障排查

| 现象 | 可能原因 | 处理方向 |
| --- | --- | --- |
| `execv` / `fork` 失败 | 未在仓库根目录执行，或未构建 `build/apps/*` | `pwd` 为仓库根；重新 `cmake --build build` |
| 订阅端 `bind: Address already in use` | 端口 9100 被占用 | 结束占用进程或改代码中的端口（需发布/订阅一致） |
| `IP_ADD_MEMBERSHIP` 失败 | 组播被禁用、无 IPv4、或环境限制 | 检查内核/容器网络模式；本机先试 `239.0.0.1` 于 lo |
| 持续 GAP | 发布端丢弃、`--rate` 过高、订阅处理过慢、或晚于发布启动 | 关 `drop-rate`、降速；编排器已先起订阅并等待 300 ms |
| `summary` 全 0 或缺字段 | summary 文件未生成或路径不可读 | 看子进程是否被信号杀死；检查 `/tmp` 权限 |
| 延迟异常高 | CPU 争用、`slow-subscriber-ratio`、或 `jitter-ms` | 对比关闭慢订阅与 jitter 的基线 |

## 退出码

| 码 | 含义 |
| --- | --- |
| `0` | 正常结束（含 `--help`） |
| `1` | 未知参数、子进程启动失败等 |

## 另见

- [RUN_SIM_ORDER.md](./RUN_SIM_ORDER.md) — TCP 订单仿真
- [NETNS_TESTING.md](./NETNS_TESTING.md) — netns + `tc netem` 集成测试
- [BENCHMARK_SUITE.md](./BENCHMARK_SUITE.md) — 基准与性能相关入口
