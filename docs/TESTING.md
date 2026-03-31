# netns Integration Testing

This document describes how to run end-to-end TCP/UDP integration scenarios inside Linux network namespaces, with repeatable network impairment (`tc netem`) and auto-generated reports.

The test entrypoint is:

- `tests/run_netns_suite.py`

---

## 1) What the suite does

In one run, the suite will:

1. Create two namespaces (`client` and `server`)
2. Create and connect a veth pair
3. Configure addresses:
   - client: `10.23.0.1/24`
   - server: `10.23.0.2/24`
4. Enable loopback and veth interfaces in both namespaces
5. Add multicast route (`239.0.0.0/8`) in both namespaces
6. Apply `tc netem` on both sides:
   - `delay 2ms 1ms`
   - `loss 1%`
   - `reorder 1%`
7. Run TCP scenario:
   - server namespace: `order_gateway`
   - client namespace: `order_client --gateway-ip 10.23.0.2 ...`
8. Run UDP scenario:
   - server namespace: `feed_subscriber`
   - client namespace: `feed_publisher`
9. Collect logs and metrics (`report.md`, `summary.json`)
10. Cleanup all processes and namespaces

---

## 2) Prerequisites

- Linux host (required for `ip netns` and `tc`)
- Root privileges (`sudo`) to manage namespaces and qdisc
- Python 3.8+
- CMake 3.16+
- Built binaries:
  - `build/apps/order_gateway`
  - `build/apps/order_client`
  - `build/apps/feed_publisher`
  - `build/apps/feed_subscriber`

Build once:

```bash
cmake -S . -B build
cmake --build build -j
```

---

## 3) Quick Start

```bash
sudo python3 tests/run_netns_suite.py \
  --build-dir build \
  --output-dir tests/netns/reports/latest \
  --duration 6
```

CLI options:

- `--build-dir` (default: `build`)
- `--output-dir` (default: `tests/netns/reports/latest`)
- `--duration` (default: `6`, seconds)

If not run as root, the script exits with:

- `This script requires root privileges (ip netns/tc).`

---

## 4) Runtime Parameters Used by the Suite

The current script uses fixed app-level parameters:

- TCP (`order_client`):
  - `--gateway-ip 10.23.0.2`
  - `--qps 120`
  - `--burst 2`
  - `--timeout-ms 800`
  - `--cancel-ratio 20`
  - `--replace-ratio 20`
- UDP (`feed_publisher`):
  - `--rate 200`
  - `--streams 2`
  - `--drop-rate 0.01`
  - `--reorder-rate 0.01`
- UDP subscriber:
  - `--subscriber-id 0`
  - `--summary-file <output-dir>/udp_summary.txt`

---

## 5) Output Artifacts

Under `--output-dir`, the suite writes:

- `report.md`: high-level pass/fail and key counters
- `summary.json`: structured metrics for TCP and UDP
- `tcp_gateway.log`: gateway logs
- `tcp_client.log`: client logs (source for TCP final metrics)
- `udp_publisher.log`: publisher logs
- `udp_subscriber.log`: subscriber logs
- `udp_summary.txt`: subscriber summary parsed by the runner

---

## 6) Pass Criteria (Current Minimal Gate)

Current checks are intentionally minimal:

- TCP pass if `ack_recv_total > 0`
- UDP pass if `recv_total > 0`

Why minimal:

- This stage validates namespace wiring and basic end-to-end liveness under impairment.
- Stronger assertions (latency percentiles, loss budget, retrans threshold) can be added as the system matures.

---

## 7) Interpreting the Report

`report.md` includes:

- `tcp_req_sent_total`
- `tcp_ack_recv_total`
- `udp_recv_total`
- `udp_gaps`
- PASS/FAIL status for TCP and UDP criteria

`summary.json` stores full parsed key-value metrics and is preferred for automation.

---

## 8) CI / Automation Recommendations

For stable regression checks:

- Use a dedicated Linux runner (avoid noisy shared hosts).
- Keep `--duration` fixed across runs.
- Archive:
  - `report.md`
  - `summary.json`
  - all scenario logs
- Fail CI if either minimal criterion fails.

Optional next step:

- Add stricter gates (for example: `ack_recv_total/req_sent_total` floor, `udp_gaps` ceiling).

---

## 9) Troubleshooting

- `Missing binary: ...`
  - Rebuild with `cmake --build build -j`.
  - Confirm `--build-dir` points to the correct build tree.
- `ip` or `tc` command failures
  - Ensure `iproute2` is installed and available in `PATH`.
  - Re-run with `sudo`.
- No metrics in `summary.json`
  - Inspect `tcp_client.log` for missing `[order_client][final]` markers.
  - Check `udp_summary.txt` and `udp_subscriber.log` for subscriber startup issues.
- Leftover state after interruption
  - The script has `finally` cleanup; if interrupted hard, manually run:
    - `sudo ip netns del <ns-name>`
