# User-Space Networking Stack (USN)

一个面向高频交易（HFT）场景的 **用户空间网络栈 / 用户态网络 IO 基础库**，通过内核旁路（Kernel Bypass）与一系列性能优化手段，实现微秒级端到端延迟。

> A minimal, production-style user‑space networking stack for low‑latency trading systems.

---

## 🎯 项目定位 / 卖点

- **面向场景**：撮合引擎、行情网关、订单网关等 HFT 组件的网络 IO 基础库
- **技术特征**：
  - 用户态 UDP/TCP 协议处理
  - Zero-copy 缓冲区与预分配内存池
  - Lock-free 队列与环形缓冲区
  - 批量收发（`recvmmsg` / `sendmmsg`）
  - NUMA-aware、cache-friendly 设计
  - epoll / io_uring / busy polling 事件驱动
- **简历价值**：能直接向面试官 / recruiter 传达你对 **latency / throughput / 内核网络栈** 有深入理解，而不是“玩具级别 demo”。

更完整的技术说明、设计细节和实现进度，请看 `docs/PROJECT_DOCUMENTATION.md`。

---

## 🧱 功能一览

- **Core**
  - Lock-free MPSC queue（多生产者单消费者队列）
  - Packet 结构与 packet ring buffer
  - 内存池 / 预分配缓冲区
- **Protocol**
  - 用户态 UDP 协议处理
  - 简化版 TCP 协议处理
- **I/O**
  - 批量收发封装（`recvmmsg` / `sendmmsg`）
  - `epoll` 封装
  - `io_uring` 封装
  - Busy polling 支持
- **Optimization**
  - Zero-copy 缓冲区（`mmap` / huge pages）
  - NUMA 拓扑探测与 CPU 亲和性工具

各模块的 API、设计 rationale 和测试情况在文档中都有详细说明。

---

## 🛠 技术栈

- **语言**：C++20
- **平台**：Linux（需要 `epoll` / `io_uring` 等系统调用）
- **依赖**：
  - `liburing`（io_uring 封装）
  - `libnuma`（NUMA 支持）
- **构建系统**：CMake 3.16+

---

## 🚀 快速开始

### 1. 克隆仓库

```bash
git clone <your-repo-url> USN-HFT-SIM
cd USN-HFT-SIM
```

### 2. 构建

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

在构建完成后，`build/` 目录中会生成测试、基准和示例可执行文件，例如：

- `usn_mpsc_queue_tests`
- `usn_mpsc_queue_bench`
- `usn_mpsc_queue_example`
- `usn_packet_ring_tests`
- `usn_udp_protocol_tests`
- `usn_tcp_protocol_tests`

> 根据你的环境与 CMake 配置，二进制名称可能略有差异，建议直接在 `build/` 目录下执行 `ls` 查看。

### 3. 运行单元测试

```bash
cd build
./usn_mpsc_queue_tests
./usn_packet_ring_tests
./usn_udp_protocol_tests
./usn_tcp_protocol_tests
```

### 4. 运行基准测试

```bash
cd build
./usn_mpsc_queue_bench
```

### 5. 运行示例

```bash
cd build
./usn_mpsc_queue_example
./usn_zero_copy_example
./usn_packet_ring_example
```

更多示例源码在 `examples/` 目录下：

- `examples/core/mpsc_queue_example.cpp`
- `examples/optimization/zero_copy_example.cpp`

---

## 📁 目录结构概览

项目遵循“头文件为主、库像系统 API 一样使用”的组织方式，核心接口全部位于 `include/usn/`：

```text
include/usn/              # 公共头文件（对外 API）
  core/                   # 核心数据结构
  protocol/               # 协议栈实现
  io/                     # IO 多路复用与批量 IO
  optimization/           # 性能优化相关模块

apps/                     # demo / app 级入口（如 market_udp / order_tcp）
tests/                    # 单元测试
examples/                 # 使用示例
benchmarks/               # 基准测试
docs/                     # 设计文档（PROJECT_DOCUMENTATION.md 等）
build/                    # 构建输出（已在 .gitignore 中）
```

典型的 include 方式如下：

```cpp
#include <usn/core/mpsc_queue.hpp>
#include <usn/protocol/udp_protocol.hpp>
#include <usn/io/epoll_wrapper.hpp>
#include <usn/optimization/zero_copy.hpp>
```

详情参考 `docs/PROJECT_DOCUMENTATION.md` 中的“项目结构”一节。

---

## ✅ 当前完成度（High-level）

- **Core**
  - Lock-free MPSC queue —— 已实现，含测试、基准和示例
  - Packet / Packet ring buffer —— 已实现并通过测试
  - Memory pool —— 已实现并集成在 packet 端到端路径中
- **I/O**
  - 批量 IO 抽象（`BatchRecv` / `BatchSend`）—— 已实现 & 测试
  - `epoll` 封装 —— 已实现并用于示例 / 测试
  - `io_uring` 封装 —— 已实现，适用于 Linux 5.10+ 环境
- **Optimization**
  - Zero-copy 缓冲区 —— 已实现，示例与测试通过
  - NUMA 工具 —— 已实现，可用于绑定线程与内存到特定 NUMA 节点
- **Protocol**
  - UDP 协议 —— 已实现，含基本测试
  - 简化版 TCP 协议 —— 已实现，用于展示用户态协议栈 pipeline

更详细的阶段划分（Phase 1–3）、每个模块的测试覆盖情况、未来 Roadmap 在 `docs/PROJECT_DOCUMENTATION.md` 中有完整列表。

---

## 🤝 适用人群与使用方式

- **想拿 HFT / infra / trading 系岗位的工程师**
  - 可以把本项目作为“用户态网络 + 低延迟 IO”方向的主项目放进简历
  - 面试时直接以本项目为载体讲解 latency budget、系统设计和优化策略
- **想系统学习 Linux 网络 + 内核旁路的同学**
  - 通过 `tests/` + `examples/` 快速理解每个模块的作用与 API
  - 对照文档理解从数据结构 → IO → 协议 → 优化的完整路径

任何问题、改进建议或性能优化想法，欢迎开 issue 或直接提 PR。
