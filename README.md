# User-Space Networking Stack

一个高性能的用户空间网络栈实现，通过内核旁路（Kernel Bypass）技术实现极低延迟的网络通信。

## 🎯 项目目标

实现一个 mini kernel networking path，包括：

- ✅ 用户空间 TCP/UDP 协议栈
- ✅ Zero-copy 缓冲区
- ✅ Lock-free 环形缓冲区
- ✅ Poll / Epoll / io_uring 支持
- ✅ Busy polling 模式
- ✅ NUMA-aware 设计

## 🚀 为什么这个项目很重要？

这是高频交易（HFT）和交易基础设施领域的核心技术，与以下工业级技术同源：

- **DPDK** (Data Plane Development Kit)
- **Solarflare** Onload
- **VMA** (Verbs Messaging Accelerator)

**招聘者看到会直接知道：你懂 latency。** ⚡

## 📋 实现路线

### Phase 1: 基础数据结构
- [x] Lock-free MPSC queue ✅
- [x] Packet ring buffer ✅
- [x] Batch recv/send ✅

### Phase 2: 高级优化
- [x] Zero-copy 实现 ✅
- [x] Cache-aligned 数据结构 ✅
- [x] NUMA-aware 设计 ✅

### Phase 3: 协议栈
- [x] UDP 实现 ✅
- [x] TCP 实现（简化版）✅

### Phase 4: 事件驱动
- [x] Poll / Epoll 集成 ✅
- [x] io_uring 集成 ✅
- [x] Busy polling ✅

## 📊 性能目标

- **延迟**：单包处理 < 1μs
- **吞吐量**：单核 > 1M packets/sec
- **P99 延迟**：< 5μs

## 📚 文档

详细的项目文档请查看 [PROJECT_DOCUMENTATION.md](./docs/PROJECT_DOCUMENTATION.md)，包含：
- 项目概述和目标
- 完整的项目结构说明
- 实现进度和模块详情
- 技术栈和性能目标
- 构建和使用指南
- 项目亮点和学习价值

## 🛠️ 技术栈

- **语言**：C++20
- **系统调用**：io_uring, epoll, mmap
- **依赖**：liburing, libnuma
- **构建系统**：CMake 3.16+

## 🚀 快速开始

### 构建项目

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 运行测试

```bash
./usn_mpsc_queue_tests
```

### 运行性能基准测试

```bash
./usn_mpsc_queue_bench
```

### 运行示例

```bash
./usn_mpsc_queue_example
```

## 📁 项目结构

```
system/
├── include/usn/              # 核心头文件库（按模块组织）
│   ├── core/                 # 核心数据结构模块
│   │   ├── mpsc_queue.hpp
│   │   ├── packet.hpp
│   │   ├── packet_ring.hpp
│   │   └── memory_pool.hpp
│   │
│   ├── protocol/             # 协议栈模块
│   │   ├── udp_protocol.hpp
│   │   ├── udp_socket.hpp
│   │   └── tcp_protocol.hpp
│   │
│   ├── io/                   # I/O 多路复用模块
│   │   ├── batch_io.hpp
│   │   ├── epoll_wrapper.hpp
│   │   ├── io_uring_wrapper.hpp
│   │   └── busy_poll.hpp
│   │
│   └── optimization/         # 性能优化模块
│       ├── zero_copy.hpp
│       └── numa_utils.hpp
│
├── tests/                    # 单元测试（按模块组织）
│   ├── core/
│   ├── protocol/
│   ├── io/
│   └── optimization/
│
├── examples/                 # 使用示例（按模块组织）
│   ├── core/
│   └── optimization/
│
├── benchmarks/               # 性能测试
│   └── core/
│
└── docs/                     # 项目文档
    ├── PROJECT_PLAN.md
    ├── PROGRESS.md
    ├── COMPLETION_SUMMARY.md
    └── STRUCTURE.md
```

**详细结构说明**: [docs/STRUCTURE.md](./docs/STRUCTURE.md)

## ✅ 已完成模块

- ✅ **Lock-Free MPSC Queue** - 多生产者单消费者无锁队列
- ✅ **Packet Ring Buffer** - 高性能数据包环形缓冲区
- ✅ **Batch I/O** - 批量网络 I/O（recvmmsg/sendmmsg）
- ✅ **Memory Pool** - 预分配内存池
- ✅ **Packet Structure** - Cache-aligned 数据包元数据
- ✅ **Zero-Copy Buffer** - mmap 零拷贝缓冲区
- ✅ **NUMA Utils** - NUMA 拓扑检测和 CPU 亲和性

查看 [PROGRESS.md](./PROGRESS.md) 了解详细进度。

## 📖 参考资源

- DPDK 编程指南
- Linux io_uring 文档
- mTCP 论文和实现
- NUMA 优化最佳实践

---

**这比写 toy OS 强 10 倍。** 💪
