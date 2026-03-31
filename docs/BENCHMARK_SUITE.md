# Benchmark Suite

`todo-17` 要求的剩余部分是两件事：

1. 统一编排基准运行
2. 自动生成可读报表

本项目现在通过 `benchmarks/run_benchmark_suite.py` 一次性完成这两步。

## 覆盖场景

- `mpsc`：`usn_mpsc_queue_bench` 微基准
- `market`：`run_sim_market`（读取 `[run_market_sim][summary]`）
- `order`：`run_sim_order`（读取 `[order_client][final]`）

## 使用方式

先确保二进制已构建：

```bash
cmake -S . -B build
cmake --build build -j
```

然后运行统一基准套件：

```bash
python3 benchmarks/run_benchmark_suite.py \
  --build-dir build \
  --output-dir benchmarks/reports/latest \
  --runs 3
```

可调参数示例：

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

## 产物

在 `--output-dir` 下会生成：

- `report.md`：可视化汇总（表格展示 median/min/max）
- `summary.json`：结构化汇总结果
- `mpsc_runs.csv` / `market_runs.csv` / `order_runs.csv`：每次运行明细
- `mpsc_raw.log` / `market_raw.log` / `order_raw.log`：原始输出留档

## 口径说明

- 报表中的核心统计口径为 `median/min/max`，用于降低偶发抖动影响。
- `market` 与 `order` 指标来源于仿真进程输出字段，字段随应用演进可扩展。
- 若要在 CI 中使用，可固定 `--runs`、速率和时长，并归档 `report.md + summary.json`。
