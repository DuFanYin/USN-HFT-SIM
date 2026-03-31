# User-Space Networking Stack (USN)

This is a **personal project for learning computer networking**. By building low-latency networking infrastructure in user space and wiring it into realistic use cases (a market data UDP path and an order TCP path), I want to understand what TCP/UDP really look like in production code and which trade-offs practitioners actually make.

In other words, this repository is both a small user-space networking stack and my own “network systems lab”.

---

## 🎯 What is this project?

- **User-space networking infrastructure**
  - Core data structures: lock-free MPSC queue, packet ring, memory pool
  - I/O abstractions: batched `recvmmsg` / `sendmmsg`, `epoll`, `io_uring`, busy polling
  - Performance tools: zero-copy, NUMA-aware pinning, cache-friendly layouts
- **Protocols and applications**
  - User-space UDP implementation (including pseudo-header checksum)
  - Simplified TCP implementation (to learn the state machine, handshake/teardown, and packet layout)
  - Demo applications:
    - Market data publisher/subscriber (UDP): `apps/market_udp/*`
    - Order client / order gateway (TCP): `apps/order_tcp/*`
- **Learning goals**
  - Walk the full path inside a “real-ish” application: data structures → I/O → protocol → application → performance.
  - Not just reading books or drawing diagrams, but compiling, running, and measuring latency / throughput.

For a more detailed feature breakdown, see `docs/FEATURES.md`. For a full design write-up, see `docs/PROJECT_DOCUMENTATION.md`. The roadmap for future work is in `TODO_ROADMAP.md`.

---

## 🛠 Tech stack (short version)

- **Language**: C++20
- **Platform**: Linux (requires `epoll` / `io_uring` and friends)
- **Dependencies**: `liburing` (optional), `libnuma` (optional)
- **Build system**: CMake 3.16+

---

## 🚀 Getting started (practical path)

### 1. Clone & build

```bash
git clone <your-repo-url> USN-HFT-SIM
cd USN-HFT-SIM

mkdir build && cd build
cmake ..
make -j$(nproc)
```

> After a successful build, all binaries will be in the `build/` directory. Use `ls` to see what was produced.

### 2. Run core tests once

```bash
cd build

# Core data structures
./usn_mpsc_queue_tests
./usn_packet_ring_tests

# Protocols
./usn_udp_protocol_tests
./usn_tcp_protocol_tests
```

### 3. Run a couple of examples to feel the data path

```bash
cd build

./usn_mpsc_queue_example
./usn_zero_copy_example
```

> For a more complete list of tests and benchmarks, check the “Tests, Benchmarks & Apps” mapping at the end of `docs/FEATURES.md`.

---

## 📁 Directory layout (overview)

```text
include/usn/              # Public headers (external API)
  core/                   # Core data structures (queue, packet, ring, memory pool)
  protocol/               # Protocol implementations (UDP/TCP)
  io/                     # I/O multiplexing and batch I/O
  optimization/           # Performance-related modules (zero-copy, NUMA, etc.)

apps/                     # Demo / app entry points (market_udp / order_tcp)
tests/                    # Unit tests
examples/                 # Usage examples
benchmarks/               # Microbenchmarks
docs/                     # Design docs (PROJECT_DOCUMENTATION.md, FEATURES.md, etc.)
build/                    # Build output (ignored by git)
```

Typical include style:

```cpp
#include <usn/core/mpsc_queue.hpp>
#include <usn/protocol/udp_protocol.hpp>
#include <usn/io/epoll_wrapper.hpp>
#include <usn/optimization/zero_copy.hpp>
```

---