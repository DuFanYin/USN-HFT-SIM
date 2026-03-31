# Benchmark Suite

This document explains how to run the unified benchmark suite, what it measures, and how to interpret the results.

The suite is implemented by `benchmarks/run_benchmark_suite.py` and executes three scenarios in one command:

- `mpsc`: micro benchmark via `build/benchmarks/usn_mpsc_queue_bench`
- `market`: UDP market simulation via `build/apps/run_sim_market`
- `order`: TCP order simulation via `build/apps/run_sim_order`

---

## 1) Prerequisites

- Linux environment
- Python 3.8+
- CMake 3.16+
- A successful build that produces:
  - `build/benchmarks/usn_mpsc_queue_bench`
  - `build/apps/run_sim_market`
  - `build/apps/run_sim_order`

Build once:

```bash
cmake -S . -B build
cmake --build build -j
```

---

## 2) Quick Start

Run with defaults:

```bash
python3 benchmarks/run_benchmark_suite.py \
  --build-dir build \
  --output-dir benchmarks/reports/latest \
  --runs 3
```

This will run each scenario `3` times and generate both human-readable and machine-readable reports.

---

## 3) CLI Options

`benchmarks/run_benchmark_suite.py` supports:

- `--build-dir` (default: `build`)
- `--output-dir` (default: `benchmarks/reports/latest`)
- `--runs` (default: `3`)
- `--market-duration` (default: `8`, seconds)
- `--market-rate` (default: `200`)
- `--order-duration` (default: `8`, seconds)
- `--order-qps` (default: `300`)
- `--timeout-s` (default: `120`)

Notes:

- The runner also uses fixed scenario knobs internally:
  - market: `--payload-size 128 --subscribers 2 --streams 2 --drop-rate 0.01 --reorder-rate 0.01`
  - order: `--burst 4 --cancel-ratio 20 --replace-ratio 20`
- `--timeout-s` is per command execution timeout.

---

## 4) Recommended Profiles

### Fast sanity run

```bash
python3 benchmarks/run_benchmark_suite.py \
  --build-dir build \
  --output-dir benchmarks/reports/quick \
  --runs 1 \
  --market-duration 3 \
  --order-duration 3 \
  --market-rate 100 \
  --order-qps 200
```

### More stable comparison run

```bash
python3 benchmarks/run_benchmark_suite.py \
  --build-dir build \
  --output-dir benchmarks/reports/stable \
  --runs 5 \
  --market-duration 10 \
  --order-duration 10 \
  --market-rate 200 \
  --order-qps 300
```

---

## 5) Output Artifacts

Under `--output-dir`, the suite writes:

- `report.md`: Markdown summary with per-metric `median/min/max`
- `summary.json`: structured summary for automation/CI
- `mpsc_runs.csv`: raw parsed metrics for each MPSC run
- `market_runs.csv`: raw parsed metrics for each market run
- `order_runs.csv`: raw parsed metrics for each order run
- `mpsc_raw.log`: concatenated raw stdout/stderr from MPSC runs
- `market_raw.log`: concatenated raw stdout/stderr from market runs
- `order_raw.log`: concatenated raw stdout/stderr from order runs

---

## 6) Metric Sources and Semantics

- `mpsc` metrics are parsed from benchmark sections marked by `[BENCH]`, including throughput (`M ops/sec`) and latency (`ns/op`).
- `market` metrics are parsed from the last line containing `[run_market_sim][summary]`.
- `order` metrics are parsed from `[order_client][final]`; if absent, fallback to `[order_client][metrics]`.

The report summarizes each numeric metric as:

- `median`: primary comparison metric (robust to outliers)
- `min` / `max`: variability bounds

---

## 7) CI / Regression Usage

For repeatable CI checks:

- Pin `--runs`, rates, and durations to fixed values.
- Use dedicated runners with stable CPU frequency and low background noise.
- Archive at least:
  - `report.md`
  - `summary.json`
  - `*_raw.log` (for post-failure inspection)

A common baseline command:

```bash
python3 benchmarks/run_benchmark_suite.py \
  --build-dir build \
  --output-dir benchmarks/reports/ci \
  --runs 3 \
  --market-duration 8 \
  --order-duration 8 \
  --market-rate 200 \
  --order-qps 300
```

---

## 8) Troubleshooting

- `FileNotFoundError: Missing benchmark binary`
  - Ensure you built with the expected `--build-dir`.
  - Re-run `cmake --build build -j`.
- Timeout failures
  - Increase `--timeout-s`.
  - Reduce `--market-duration`, `--order-duration`, or rates.
- Empty/partial parsed metrics
  - Inspect `market_raw.log` / `order_raw.log` for changed log markers.
  - If output format changed, update parser logic in `run_benchmark_suite.py`.
