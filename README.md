# User-Space Networking Stack (USN)

This is a **personal project for learning computer networking**.

It builds a low-latency user-space networking infrastructure and wires it into two HFT-style use cases:
- a market data UDP path
- an order TCP path

My goal is to understand what TCP/UDP look like in production-oriented code and what trade-offs practitioners make in real systems.

My learning workflow is to use AI to generate the initial skeleton and the minimum implementation needed to make the end-to-end flow run. I then read through the code to understand it, and iteratively improve correctness, completeness, and performance.


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

For a more detailed feature breakdown, see `docs/FEATURES.md`. For a full design write-up, see `docs/PROJECT_DOCUMENTATION.md`. The roadmap for future work is in `TODO_ROADMAP.md`, and acceptance criteria are in `docs/ROADMAP_ACCEPTANCE.md`. The unified benchmark suite usage is in `docs/BENCHMARK_SUITE.md`, and netns integration testing is in `docs/NETNS_TESTING.md` (runner path: `tests/run_netns_suite.py`).

---

## 🛠 Tech stack (short version)

- **Language**: C++20
- **Platform**: Linux (requires `epoll` / `io_uring` and friends)
- **Dependencies**: `liburing` (optional), `libnuma` (optional)
- **Build system**: CMake 3.16+

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
benchmarks/               # Microbenchmarks
docs/                     # Design docs (PROJECT_DOCUMENTATION.md, FEATURES.md, etc.)
```

---