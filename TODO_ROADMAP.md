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

- [ ] todo-02-udp-spec-and-parser
  - 为 UDP 模块写清晰的“协议语义规格”：校验和/长度/端口处理策略；实现并固化 UDP 报文解析与序列化路径（含边界条件与错误处理）。

- [ ] todo-03-udp-jitter-timers
  - 补齐 UDP 端到端语义：乱序/丢包的检测与上层反馈接口设计（即使不做重传，也要让上层能感知）。

- [ ] todo-04-recv-send-interruptibility
  - 统一 IO 的可取消/可超时语义：为 batch recv/send 与事件驱动（epoll/io_uring/busy poll）加入超时、取消点、错误码体系。

- [ ] todo-05-packet-ring-contract
  - 给 Packet / packet ring buffer 定义严格契约：生命周期、对齐、可见性/内存序、零拷贝语义（谁拥有内存、何时释放/回收）。并加入契约测试。

- [ ] todo-06-connectionless-debugging
  - 增加网络调试能力：可选的 tracing/采样（packet id、时间戳、queue latency、syscall batching stats），并让测试可稳定复现。

- [ ] todo-07-tcp-core-state-machine
  - TCP 连接建立与状态机（学习优先、简化可迭代）：实现 CLOSED→LISTEN→SYN_RCVD→ESTABLISHED 等关键状态；完善握手报文收发与参数验证。

- [ ] todo-08-tcp-seq-ack-engine
  - TCP 序列号/确认号引擎：实现发送序号、接收窗口、ACK 生成与处理；支持基本 out-of-order 缓冲与重组接口。

- [ ] todo-09-tcp-retransmit-timers
  - TCP 重传与定时器：RTO 机制（可简化为固定/指数退避）、丢包检测、未确认数据的重传调度。

- [ ] todo-10-tcp-flow-control
  - TCP 流量控制：接收窗口（rwnd）计算、窗口更新（window update）、与重传/发送节流的联动。

- [ ] todo-11-tcp-congestion-control
  - TCP 拥塞控制（学习/可迭代）：从简化慢启动 + 拥塞避免开始，定义 cwnd/ssthresh 以及与 RTT/丢包信号的关系。

- [ ] todo-12-tcp-keepalive-and-teardown
  - 连接释放与保活：实现 FIN/ACK 关闭流程（至少能正确收尾），并补齐 keepalive 或心跳机制的接口（可选）。

- [ ] todo-13-tcp-sack-or-reorder
  - 面向“更贴近真实”：实现基本 SACK 或替代的重组/空洞标记机制（至少能让 out-of-order 数据的处理路径更完整）。

- [ ] todo-14-tcp-checksum-and-malformed
  - 健壮性：校验伪首部/校验和（若使用伪头部）、处理畸形报文、非法状态转换、重复包/重复 ACK 的容忍。

- [ ] todo-15-integration-tests-netns
  - 集成测试基建：用 `netns`/虚拟网桥做可重复的 TCP/UDP 场景测试（乱序、丢包、延迟注入），形成回归测试集。

- [ ] todo-16-cross-io-backends-consistency
  - 一致性测试：对 epoll / io_uring / busy polling 三种事件后端，验证协议行为一致（时序允许差异，但语义必须一致）。

- [ ] todo-17-performance-benchmark-suite
  - 建立性能基准体系：按数据路径拆分（queue, ring, batch io, protocol encode/decode, copy/no-copy），并加入 P99/最大延迟统计与可视化输出。

- [ ] todo-18-atomic-and-cache-profiler
  - 加入可观测的性能剖析：CPU profiling、cache miss/branch miss 的采集方式（如 `perf stat`），对热点函数给出优化清单。

- [ ] todo-19-documentation-for-learning
  - 为“完整理解计算机网络”补齐文档：写清每一步为什么这么做（状态机图、序号图、窗口图、重传时序图），并把文档与测试用例绑定。

- [ ] todo-20-examples-tcp-end-to-end
  - 补齐端到端示例：至少一个 UDP 发送/接收示例 + 一个 TCP 客户端/服务端示例（可在本机/容器内跑），并在 README 中写清运行方式。

- [ ] todo-21-build-and-deps-portability
  - 构建/依赖可靠性：对 Linux 依赖项做检查与编译选项（io_uring/liburing、numa/libnuma），提升可迁移性与错误提示。

- [ ] todo-22-ci-basic
  - 基础 CI：至少跑单元测试 + 关键集成测试（TCP 握手/数据收发/关闭等），并生成性能基线（可选，轻量级）。

