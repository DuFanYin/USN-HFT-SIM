# User-Space Networking Stack (USN)

这是一个**用来学习计算机网络的个人项目**：我通过亲手实现用户态的低延迟网络基础设施，再把它拼成接近真实场景的应用（行情 UDP 通路、订单 TCP 通路），来理解 TCP/UDP 在工程里真正长什么样、有哪些取舍。

换句话说，这个仓库既是一个简单的用户空间网络栈，也是我自己的“网络系统实验室”。

---

## 🎯 项目是什么？

- **用户态网络基础设施**
  - 核心数据结构：lock-free MPSC 队列、packet ring、内存池
  - I/O 抽象：批量 `recvmmsg` / `sendmmsg`、`epoll`、`io_uring`、busy polling
  - 性能工具：zero-copy、NUMA-aware 绑定、cache-friendly 布局
- **协议与应用**
  - 用户态 UDP 协议处理（含伪头部校验和）
  - 简化版 TCP 协议（用于理解状态机、握手/关闭、报文结构）
  - Demo 应用：
    - 行情发布/订阅（UDP）：`apps/market_udp/*`
    - 订单客户端/网关（TCP）：`apps/order_tcp/*`
- **学习目标**
  - 在“真实一点的应用”里，走完整条路径：数据结构 → I/O → 协议 → 应用 → 性能
  - 不只是看书/画图，而是可以编译、跑起来、测延迟和吞吐。

更细的功能拆分见 `docs/FEATURES.md`，完整设计说明见 `docs/PROJECT_DOCUMENTATION.md`，后续计划见 `TODO_ROADMAP.md`。

另外，两个仿真总控 app 的行为说明见：

- `docs/RUN_SIM_MARKET.md`
- `docs/RUN_SIM_ORDER.md`

---

## 🛠 技术栈（很短版）

- **语言**：C++20
- **平台**：Linux（需要 `epoll` / `io_uring` 等）
- **依赖**：`liburing`（可选）、`libnuma`（可选）
- **构建**：CMake 3.16+

---

## 🚀 快速开始（只保留最常用的）

### 1. 克隆 + 构建

```bash
git clone <your-repo-url> USN-HFT-SIM
cd USN-HFT-SIM

mkdir build && cd build
cmake ..
make -j$(nproc)
```

> 编译成功后，所有可执行文件都会出现在 `build/` 目录下，可用 `ls` 自己看一眼。

### 2. 跑一轮核心测试

```bash
cd build

# 核心数据结构
./usn_mpsc_queue_tests
./usn_packet_ring_tests

# 协议
./usn_udp_protocol_tests
./usn_tcp_protocol_tests
```

### 3. 跑一个示例，感受数据路径

```bash
cd build

./usn_mpsc_queue_example
./usn_zero_copy_example
```

---

## 📁 目录结构（概览）

```text
include/usn/              # 公共头文件（对外 API）
  core/                   # 核心数据结构（队列、packet、ring、内存池）
  protocol/               # 协议栈实现（UDP/TCP）
  io/                     # IO 多路复用与批量 IO
  optimization/           # 性能优化相关模块（zero-copy、NUMA 等）

apps/                     # demo / app 级入口（market_udp / order_tcp）
tests/                    # 单元测试
examples/                 # 使用示例
benchmarks/               # 性能基准
docs/                     # 设计文档（PROJECT_DOCUMENTATION.md、FEATURES.md 等）
build/                    # 构建输出（已在 .gitignore 中）
```

典型的 include 方式：

```cpp
#include <usn/core/mpsc_queue.hpp>
#include <usn/protocol/udp_protocol.hpp>
#include <usn/io/epoll_wrapper.hpp>
#include <usn/optimization/zero_copy.hpp>
```

---