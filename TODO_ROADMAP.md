# TODO Roadmap

目标：以“用户态高性能网络 IO”为基石，逐步构建一个贴合真实网络语义的学习/工程系统，形成对 **TCP / UDP** 的完整理解。

说明：
- `IN PROGRESS`：当前正在推进
- `PENDING`：待开始

---

## IN PROGRESS

- [ ] todo-01-roadmap-acceptance
  - 补齐项目路线与验收标准：明确“理解贴近真实 TCP/UDP”所需的功能清单、性能指标口径与测试准入规则（例如延迟/吞吐/正确性）。

---

## PENDING

- [x] todo-02-udp-spec-and-parser ✅
  - 为 UDP 模块写清晰的”协议语义规格”：校验和/长度/端口处理策略；实现并固化 UDP 报文解析与序列化路径（含边界条件与错误处理）。
  - **已完成**：`UdpProtocol` 实现了 encapsulate/parse/verify_checksum 完整路径，含伪首部校验和、长度校验、边界检查；测试已覆盖。

- [x] todo-03-udp-jitter-timers ✅（部分）
  - 补齐 UDP 端到端语义：乱序/丢包的检测与上层反馈接口设计（即使不做重传，也要让上层能感知）。
  - **已完成部分**：`run_sim_market` / `feed_publisher` / `feed_subscriber` 已支持 `drop/reorder/jitter` 注入与 `gaps/reorder_events/max_reorder_depth` 指标输出；剩余：尚未沉淀为库级统一反馈接口（目前主要在 app 层）。

- [ ] todo-04-recv-send-interruptibility
  - 统一 IO 的可取消/可超时语义：为 batch recv/send 与事件驱动（epoll/io_uring/busy poll）加入超时、取消点、错误码体系。

- [x] todo-05-packet-ring-contract ✅（部分）
  - 给 Packet / packet ring buffer 定义严格契约：生命周期、对齐、可见性/内存序、零拷贝语义（谁拥有内存、何时释放/回收）。并加入契约测试。
  - **已完成部分**：协议函数已去掉内部 `new[]`，强制调用方通过 MemoryPool/ZeroCopyMemoryPool 传入 buffer，生命周期由池管理；PacketRing 已在 gateway 和 subscriber 中实际使用。剩余：契约测试尚未补齐。

- [x] todo-06-connectionless-debugging ✅（部分）
  - 增加网络调试能力：可选的 tracing/采样（packet id、时间戳、queue latency、syscall batching stats），并让测试可稳定复现。
  - **已完成部分**：已在模拟链路中输出稳定可采样指标（seq、时间戳、延迟分位、gap/reorder/backpressure 等）并支持参数化复现实验；剩余：尚未形成统一 tracing 框架与系统调用批处理统计。

- [x] todo-07-tcp-core-state-machine ✅（部分）
  - TCP 连接建立与状态机（学习优先、简化可迭代）：实现 CLOSED→LISTEN→SYN_RCVD→ESTABLISHED 等关键状态；完善握手报文收发与参数验证。
  - **已完成部分**：`TcpProtocol` 已具备 SYN/SYN-ACK/ACK/FIN 报文构造与解析；`run_sim_order` 客户端增加 `CONNECTING/ACTIVE/RETRYING/STOPPED` 运行态输出。剩余：协议级完整连接状态机与严格状态迁移校验仍未完成。

- [x] todo-08-tcp-seq-ack-engine ✅（部分）
  - TCP 序列号/确认号引擎：实现发送序号、接收窗口、ACK 生成与处理；支持基本 out-of-order 缓冲与重组接口。
  - **已完成部分**：已在 TCP 路径中维护基础 seq/ack 字段并进行 ACK 处理与回显校验；剩余：out-of-order 缓冲/重组与窗口联动尚未实现。

- [x] todo-09-tcp-retransmit-timers ✅（部分）
  - TCP 重传与定时器：RTO 机制（可简化为固定/指数退避）、丢包检测、未确认数据的重传调度。
  - **已完成部分**：客户端已支持 timeout、断连重连与失败计数的实验路径；剩余：真正的未确认段重传调度与 RTO 算法未落地。

- [x] todo-10-tcp-flow-control ✅（部分）
  - TCP 流量控制：接收窗口（rwnd）计算、窗口更新（window update）、与重传/发送节流的联动。
  - **已完成部分**：网关已支持背压阈值与命中统计（app 层）；剩余：协议级 rwnd/window update 与发送节流联动未实现。

- [ ] todo-11-tcp-congestion-control
  - TCP 拥塞控制（学习/可迭代）：从简化慢启动 + 拥塞避免开始，定义 cwnd/ssthresh 以及与 RTT/丢包信号的关系。

- [x] todo-12-tcp-keepalive-and-teardown ✅（部分）
  - 连接释放与保活：实现 FIN/ACK 关闭流程（至少能正确收尾），并补齐 keepalive 或心跳机制的接口（可选）。
  - **已完成部分**：`TcpProtocol` 已有 FIN 报文构造，模拟链路具备断连与重连实验能力；剩余：完整 FIN/ACK 收尾状态机与 keepalive 机制未完成。

- [ ] todo-13-tcp-sack-or-reorder
  - 面向“更贴近真实”：实现基本 SACK 或替代的重组/空洞标记机制（至少能让 out-of-order 数据的处理路径更完整）。

- [ ] todo-14-tcp-checksum-and-malformed
  - 健壮性：校验伪首部/校验和（若使用伪头部）、处理畸形报文、非法状态转换、重复包/重复 ACK 的容忍。

- [ ] todo-15-integration-tests-netns
  - 集成测试基建：用 `netns`/虚拟网桥做可重复的 TCP/UDP 场景测试（乱序、丢包、延迟注入），形成回归测试集。

- [ ] todo-16-cross-io-backends-consistency
  - 一致性测试：对 epoll / io_uring / busy polling 三种事件后端，验证协议行为一致（时序允许差异，但语义必须一致）。

- [x] todo-17-performance-benchmark-suite ✅（部分）
  - 建立性能基准体系：按数据路径拆分（queue, ring, batch io, protocol encode/decode, copy/no-copy），并加入 P99/最大延迟统计与可视化输出。
  - **已完成部分**：模拟链路已输出 p50/p99/p999/max 与吞吐相关统计，支持参数化压测；剩余：统一基准套件编排与可视化报表尚未完成。

- [ ] todo-18-atomic-and-cache-profiler
  - 加入可观测的性能剖析：CPU profiling、cache miss/branch miss 的采集方式（如 `perf stat`），对热点函数给出优化清单。

- [ ] todo-19-documentation-for-learning
  - 为“完整理解计算机网络”补齐文档：写清每一步为什么这么做（状态机图、序号图、窗口图、重传时序图），并把文档与测试用例绑定。

- [x] todo-20-examples-tcp-end-to-end ✅
  - 补齐端到端示例：至少一个 UDP 发送/接收示例 + 一个 TCP 客户端/服务端示例（可在本机/容器内跑），并在 README 中写清运行方式。
  - **已完成**：`run_sim_market`（UDP 组播行情收发）和 `run_sim_order`（TCP 订单请求-应答），均可本机运行；行为说明见 `docs/RUN_SIM_MARKET.md` 和 `docs/RUN_SIM_ORDER.md`。

- [x] todo-21-build-and-deps-portability ✅（部分）
  - 构建/依赖可靠性：对 Linux 依赖项做检查与编译选项（io_uring/liburing、numa/libnuma），提升可迁移性与错误提示。
  - **已完成部分**：CMake 已对 libnuma/liburing 做 find 检查并链接；项目已限定 Linux-only 编译路径。剩余：缺失时的友好错误提示可进一步完善。

- [ ] todo-22-ci-basic
  - 基础 CI：至少跑单元测试 + 关键集成测试（TCP 握手/数据收发/关闭等），并生成性能基线（可选，轻量级）。

