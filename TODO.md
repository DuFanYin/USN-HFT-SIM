# TODO — HFT 高性能演进路线

> 以当前 USN-HFT-SIM 基础设施为起点，围绕延迟、吞吐、确定性三个维度的升级方向。

---

## 1. 关键路径延迟压缩

### 1.1 Kernel Bypass 网络栈
- [ ] 集成 DPDK 替代内核 UDP 收发，目标 < 1 μs wire-to-app
- [ ] 评估 AF_XDP (eBPF) 作为轻量替代方案，对比 DPDK 的部署复杂度
- [ ] 实现 raw socket + `SO_TIMESTAMPING` 硬件时间戳支持，精确测量 NIC 到 userspace 延迟

### 1.2 io_uring 深度优化
- [ ] 当前 `IoUringWrapper` 仅使用基础提交/完成路径，启用 `IORING_SETUP_SQPOLL` 消除 syscall 开销
- [ ] 实现 registered buffers (`io_uring_register_buffers`) 避免每次 I/O 的 DMA 映射
- [ ] 支持 fixed file descriptors (`IORING_REGISTER_FILES`) 减少 fd 查找开销
- [ ] 实现 multishot recv (`IORING_RECV_MULTISHOT`) 用于 UDP 批量接收

### 1.3 零拷贝发送路径
- [ ] TCP 发送路径启用 `MSG_ZEROCOPY` + completion notification
- [ ] UDP 组播发布端使用 `sendmmsg` + `MSG_ZEROCOPY` 组合
- [ ] 评估 `splice`/`vmsplice` 在 TCP 大包场景的适用性

---

## 2. 内存子系统

### 2.1 分配器演进
- [ ] 当前 `MemoryPool` 为单线程 freelist；实现 per-thread slab allocator，消除跨线程竞争
- [ ] 引入 hugepage (2MB/1GB) 支持，减少 TLB miss；`mmap(MAP_HUGETLB)` 或 `libhugetlbfs`
- [ ] 实现 arena allocator 用于请求级别的生命周期管理（一次分配，批量释放）

### 2.2 数据结构 Cache 优化
- [ ] `PacketRing` 改用 padding 隔离 head/tail 到不同 cache line，消除 false sharing
- [ ] `TcpReassemblyBuffer` 当前使用 `std::map`；改用 flat array + bitmap 实现 O(1) 插入/查找
- [ ] `TcpRetransmissionQueue` 中 `contains()` 为 O(n) 线性扫描；改用有序数组 + 二分或 hashmap
- [ ] 热路径结构体（OrderRequest, OrderAck）确保 sizeof 对齐到 cache line 或半 cache line

### 2.3 对象池化
- [ ] 池化 `std::vector<uint8_t>` 替代重传路径中的频繁 heap 分配
- [ ] epoll event vector 预分配并复用，避免每次 `wait_with_options` 的 resize

---

## 3. CPU 与调度

### 3.1 线程模型
- [ ] 实现 pinned thread pool：每个核运行一个事件循环，按功能划分（网络 I/O / 协议处理 / 业务逻辑）
- [ ] 关键路径线程设置 `SCHED_FIFO` 实时调度策略 + `mlockall(MCL_CURRENT | MCL_FUTURE)`
- [ ] 隔离 CPU（`isolcpus` + `nohz_full`）给关键路径线程，避免内核调度干扰

### 3.2 Busy Polling 升级
- [ ] 当前 `BusyPoller` 使用 `std::function` 回调；改为模板参数消除虚调用 / 间接跳转开销
- [ ] 实现 adaptive spinning：根据历史数据到达率动态调整 spin 轮次与 pause 指令插入
- [ ] 支持 `SO_BUSY_POLL` 内核参数联动，在 socket 级别启用忙轮询

### 3.3 NUMA 感知增强
- [ ] 当前 `numa_utils.hpp` 只做 CPU affinity；扩展为 NUMA-local 内存分配（`mbind` / `set_mempolicy`）
- [ ] 跨 NUMA 节点通信使用 shared memory ring buffer，避免 remote memory access

---

## 4. 协议栈演进

### 4.1 TCP 优化
- [ ] 实现 TCP Fast Open (TFO) 减少建连 RTT
- [ ] 拥塞控制器 (`TcpCongestionController`) 初始窗口从 1 MSS 提升到 IW=10 (RFC 6928)
- [ ] 支持 Selective ACK (SACK) 避免不必要的全量重传
- [ ] 实现 Nagle 算法开关 + TCP_CORK 批量聚合小包

### 4.2 UDP 可靠性层
- [ ] 在 `UdpDeliveryFeedback` 基础上实现轻量 NACK 协议，gap 主动请求重传
- [ ] 支持 FEC (Forward Error Correction) 前向纠错，适用于组播行情场景
- [ ] 实现 sequence-based dedup + jitter buffer 处理乱序与重复

### 4.3 序列化
- [ ] 当前使用 `memcpy` 裸结构体；评估 SBE (Simple Binary Encoding) 用于零开销编解码
- [ ] 确保所有 wire format 结构体为 `__attribute__((packed))` 且有 static_assert 校验大小
- [ ] 支持 protocol version field 实现向前兼容的消息演进

---

## 5. 可观测性与调试

### 5.1 延迟追踪
- [ ] 当前 `Tracer` 输出到 stdout；改为写入 lock-free ring buffer，由独立线程异步落盘
- [ ] 支持 per-packet 全链路时间戳标注（NIC rx → kernel → userspace → app → tx）
- [ ] 集成 `perf_event_open` 读取硬件 PMC（cache miss, branch mispredict, IPC）

### 5.2 实时指标
- [ ] 实现 lock-free HDR Histogram 用于 p50/p99/p999 延迟分位数在线计算
- [ ] 暴露 Prometheus-compatible `/metrics` endpoint 或共享内存指标页
- [ ] 添加 CPU utilization、context switch、page fault 的实时监控

### 5.3 性能回归检测
- [ ] 基于 `benchmarks/` 框架建立 CI 基准测试，PR 合并前自动检测延迟回归
- [ ] 定义 SLA 阈值（如 p99 < 10 μs），超出自动告警

---

## 6. 构建与工程

### 6.1 编译优化
- [ ] 启用 LTO (Link-Time Optimization) 跨翻译单元内联
- [ ] 关键路径代码使用 PGO (Profile-Guided Optimization) 优化分支预测
- [ ] 评估 `-march=native` + `-mtune=native` 在目标硬件上的收益
- [ ] 热路径函数添加 `[[gnu::hot]]` / `__attribute__((hot))` 提示

### 6.2 测试基础设施
- [ ] 当前 netns 测试需要 root；实现 unprivileged 用户态协议栈测试模式
- [ ] 添加 chaos 测试：随机丢包、延迟注入、乱序、重复，验证协议栈健壮性
- [ ] 实现 deterministic replay：录制网络流量，离线回放验证处理逻辑一致性

---

## 7. 性能捕捉与测量方法论

> 每一项优化必须可量化。以下按层级给出测量手段，确保优化前后有对比基线、有回归防线。

### 7.1 微基准测试（Microbenchmark）

针对单个组件的隔离测量，排除系统噪声。

| 测量目标 | 方法 | 工具 | 示例命令 / 集成点 |
|----------|------|------|-------------------|
| 内存分配延迟 | 循环 allocate/deallocate 取 p50/p99 | Google Benchmark | `bm.SetLabel("slab_alloc"); for (auto _ : state) pool.allocate();` |
| PacketRing 吞吐 | 单生产者单消费者 push/pop 吞吐 ops/s | Google Benchmark | 固定消息大小，测量 10M 次操作耗时 |
| TCP 帧创建/解析 | `create_data` + `parse` 往返延迟 | `rdtsc` / `std::chrono::steady_clock` | 直接在 `benchmarks/protocol/` 下新增 |
| 序列化开销 | memcpy vs SBE 编解码 per-message ns | Google Benchmark + `perf stat` | 对比 IPC、L1-dcache-load-misses |
| Busy poll 空转开销 | 无数据时单次 poll 循环耗时 | `rdtsc` 差值 | 衡量回调开销，验证模板化消除虚调用效果 |

**关键原则**：每个 benchmark 输出 JSON 格式结果，存入 `benchmarks/results/`，CI 可 diff。

### 7.2 系统级延迟测量

端到端路径延迟，覆盖 wire → app → wire 全链路。

#### 7.2.1 时间戳注入法
```
发送端                                              接收端
  │                                                    │
  ├─ T1: steady_clock 写入 OrderRequest.send_ts_ns     │
  │                                                    │
  │  ─── 网络传输 ───>                                  │
  │                                                    │
  │                                    T2: 收到后取 now │
  │                                    one_way = T2-T1  │
  │                                                    │
  │  <─── ACK ────                                     │
  │                                                    │
  ├─ T3: 收到 ACK 后取 now                              │
  │  rtt = T3 - T1                                     │
```
- [ ] 在现有 `Tracer` 基础上为每个 packet 注入 4 个时间戳点：`app_tx`, `wire_tx`, `wire_rx`, `app_rx`
- [ ] 使用 `SO_TIMESTAMPING` + `SCM_TIMESTAMPING` 获取内核/硬件级时间戳，与 app 时间戳对比得出内核开销

#### 7.2.2 往返延迟分布采集
- [ ] 采集 RTT 样本写入 mmap ring buffer，后处理生成 HDR Histogram
- [ ] 输出分位数：p50, p90, p99, p99.9, p99.99, max, stdev, jitter (p99-p50)
- [ ] 区分 new/cancel/replace 三种 order action 的延迟分布（已有 `rtt_new`, `rtt_cancel`, `rtt_replace`）

### 7.3 硬件性能计数器（PMC）

用 CPU 硬件事件定位瓶颈根因，而非猜测。

| 事件 | 含义 | 优化方向 |
|------|------|----------|
| `L1-dcache-load-misses` | L1 数据缓存未命中 | 数据结构 cache line 对齐、减小 working set |
| `LLC-load-misses` | 最后一级缓存未命中 | hugepage 减少 TLB miss，NUMA-local 分配 |
| `dTLB-load-misses` | 数据 TLB 未命中 | hugepage 直接命中 |
| `branch-misses` | 分支预测失败 | PGO、`[[likely]]`/`[[unlikely]]` 提示 |
| `instructions` / `cycles` | IPC (Instructions Per Cycle) | 整体执行效率指标 |
| `context-switches` | 上下文切换次数 | `isolcpus` + `SCHED_FIFO` 效果验证 |
| `page-faults` | 缺页中断 | `mlockall` + prefault 效果验证 |

```bash
# 单次运行采集
perf stat -e cycles,instructions,L1-dcache-load-misses,LLC-load-misses,dTLB-load-misses,branch-misses \
    ./build/order_client --qps 10000 --burst 8

# 采样热点函数
perf record -g -F 10000 ./build/order_gateway
perf report --stdio --sort=overhead
```

- [ ] 封装为 `benchmarks/run_perf_profile.sh`，一键采集并输出摘要
- [ ] 在 `Tracer` 中可选集成 `perf_event_open` 读取 per-thread PMC，随业务日志一起输出

### 7.4 吞吐与背压测量

| 指标 | 计算方式 | 采集点 |
|------|----------|--------|
| 消息吞吐 (msg/s) | `req_sent_total` / elapsed_seconds | order_client 周期报告 |
| 字节吞吐 (MB/s) | msg/s × sizeof(OrderRequest+TcpHeader) | 同上 |
| 有效利用率 | data_hits / iterations（BusyPollResult 已有） | feed_subscriber busy poll |
| 背压频率 | `backpressure_hits` / `req_recv_total` | order_gateway |
| 拥塞窗口利用率 | `cwnd_blocked_total` / `req_sent_total` | order_client |
| 重传率 | `retransmit_total` / `req_sent_total` | order_client |
| 丢包率 | `gaps` / `total` | feed_subscriber UdpDeliveryFeedback |

- [ ] 将上述指标结构化输出为 CSV/JSON，便于绘图对比
- [ ] 实现 sliding window 计算瞬时吞吐（最近 1s），而非全局累计平均

### 7.5 抖动与确定性分析

HFT 不仅要求低延迟，更要求延迟**稳定**。

- [ ] **Coordinated Omission 修正**：当系统过载时，慢请求会阻塞后续请求的发送，导致采样偏低。参考 Gil Tene 方法，按预期发送间隔补偿漏测的样本
- [ ] **尾延迟追踪**：当 p99 突增时，记录当时的系统状态快照（context switch 数、page fault 数、ring buffer 水位）用于事后分析
- [ ] **抖动指标**：
  - `jitter = p99 - p50`（绝对抖动）
  - `cv = stdev / mean`（变异系数）
  - `max / p50` 比值（尾部放大倍数）
- [ ] **热身排除**：前 N 秒 / 前 M 条消息标记为 warmup，不计入统计（JIT、page fault、cache cold 的影响）

### 7.6 A/B 对比框架

每次优化必须有 before/after 对比。

```
benchmarks/
├── results/
│   ├── baseline_20260401.json      # 优化前基线
│   └── after_hugepage_20260402.json
├── compare_results.py              # 自动 diff 两次结果
└── run_benchmark_suite.py          # 一键跑全套
```

- [ ] `compare_results.py`：读取两份 JSON，输出各指标的变化百分比，标红回归项
- [ ] CI 集成：PR 提交时自动跑 benchmark suite，与 main 分支基线对比，延迟回归 > 5% 则 fail
- [ ] 每次优化合并后更新基线快照，存入 `benchmarks/results/` 作为下次对比的参照

### 7.7 持续监控（生产/长时间运行）

- [ ] 共享内存指标页：关键计数器写入 `mmap` 共享内存，外部进程（Prometheus exporter / 自定义 reader）读取，零开销暴露
- [ ] Grafana 看板定义：latency p99 曲线、throughput 曲线、retransmit rate 曲线、CPU PMC 曲线，4 panel 一屏
- [ ] 告警规则：
  - p99 > 2× baseline → warning
  - p99 > 5× baseline → critical
  - retransmit_rate > 1% → warning
  - context_switches/s > 100 → warning（关键路径线程）

---

## 优先级建议

| 阶段 | 重点 | 预期收益 |
|------|------|----------|
| P0 | hugepage + SQPOLL + cache line padding + IW=10 | 立即可测量的延迟改善 |
| P1 | kernel bypass (AF_XDP) + per-thread slab + 实时调度 | 突破 kernel 瓶颈 |
| P2 | DPDK 全栈 + SBE 序列化 + HDR Histogram | 生产级 HFT 水平 |
| P3 | FEC + hardware timestamping + PGO | 极致优化 |
