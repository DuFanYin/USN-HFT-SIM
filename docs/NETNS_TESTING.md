# netns Integration Testing

`todo-15` 的目标是把 TCP/UDP 场景从单机模拟推进到 Linux 网络命名空间中做可重复回归。

当前提供统一入口：

- `tests/run_netns_suite.py`

它会自动完成：

1. 创建两个 netns（client/server）
2. 建立 veth 并配置地址
3. 注入 `tc netem`（delay/loss/reorder）
4. 运行 TCP 场景（`order_client` -> `order_gateway`）
5. 运行 UDP 场景（`feed_publisher` -> `feed_subscriber`）
6. 收集结果并写报告
7. 自动清理 netns

## 运行前提

- Linux
- root 权限（需要 `ip netns` 与 `tc`）
- 已构建以下二进制：
  - `build/apps/order_gateway`
  - `build/apps/order_client`
  - `build/apps/feed_publisher`
  - `build/apps/feed_subscriber`

## 运行命令

```bash
cmake -S . -B build
cmake --build build -j

sudo python3 tests/run_netns_suite.py \
  --build-dir build \
  --output-dir tests/netns/reports/latest \
  --duration 6
```

## 产物

- `tests/netns/reports/latest/report.md`
- `tests/netns/reports/latest/summary.json`
- `tests/netns/reports/latest/tcp_gateway.log`
- `tests/netns/reports/latest/tcp_client.log`
- `tests/netns/reports/latest/udp_publisher.log`
- `tests/netns/reports/latest/udp_subscriber.log`

## 当前门槛（最小版）

- TCP：`ack_recv_total > 0`
- UDP：`recv_total > 0`

后续可把门槛扩展到延迟分位、丢包率、重传计数和稳定性窗口。
