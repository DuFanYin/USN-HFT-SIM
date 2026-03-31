#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Unified benchmark suite runner and report generator.

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import re
import statistics
import subprocess
from pathlib import Path
from typing import Dict, List, Tuple


KV_PATTERN = re.compile(r"([a-zA-Z0-9_]+)=([^\s]+)")
FLOAT_PATTERN = re.compile(r"([0-9]+(?:\.[0-9]+)?)")


def parse_keyvals(line: str) -> Dict[str, float]:
    out: Dict[str, float] = {}
    for k, v in KV_PATTERN.findall(line):
        try:
            out[k] = float(v)
        except ValueError:
            continue
    return out


def parse_mpsc_metrics(output: str) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    sections: List[Tuple[str, str]] = []
    current = ""
    bucket: List[str] = []

    for line in output.splitlines():
        if line.startswith("[BENCH]"):
            if current and bucket:
                sections.append((current, "\n".join(bucket)))
            current = line.replace("[BENCH]", "").strip().replace(" ", "_").lower()
            bucket = []
        else:
            bucket.append(line)
    if current and bucket:
        sections.append((current, "\n".join(bucket)))

    for name, content in sections:
        t_match = re.search(r"Throughput:\s*([0-9]+(?:\.[0-9]+)?)\s*M ops/sec", content)
        l_match = re.search(r"Latency:\s*([0-9]+(?:\.[0-9]+)?)\s*ns/op", content)
        if t_match:
            metrics[f"mpsc_{name}_throughput_mops"] = float(t_match.group(1))
        if l_match:
            metrics[f"mpsc_{name}_latency_ns"] = float(l_match.group(1))
    return metrics


def parse_line_by_prefix(output: str, prefix: str) -> Dict[str, float]:
    selected = ""
    for line in output.splitlines():
        if prefix in line:
            selected = line
    return parse_keyvals(selected) if selected else {}


def run_cmd(cmd: List[str], timeout_s: int, cwd: Path | None = None) -> str:
    proc = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout_s,
        check=True,
        cwd=str(cwd) if cwd else None,
    )
    return proc.stdout + ("\n" + proc.stderr if proc.stderr else "")


def summarize_numeric_rows(rows: List[Dict[str, float]]) -> Dict[str, float]:
    keys = sorted({k for row in rows for k in row.keys()})
    summary: Dict[str, float] = {}
    for k in keys:
        vals = [row[k] for row in rows if k in row]
        if not vals:
            continue
        summary[f"{k}_median"] = statistics.median(vals)
        summary[f"{k}_min"] = min(vals)
        summary[f"{k}_max"] = max(vals)
    return summary


def write_csv(path: Path, rows: List[Dict[str, float]]) -> None:
    keys = sorted({k for row in rows for k in row.keys()})
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["run"] + keys)
        writer.writeheader()
        for idx, row in enumerate(rows, start=1):
            out = {"run": idx}
            out.update(row)
            writer.writerow(out)


def format_table(title: str, summary: Dict[str, float]) -> str:
    lines = [f"### {title}", "", "| metric | median | min | max |", "|---|---:|---:|---:|"]
    base_metrics = sorted({k.rsplit("_", 1)[0] for k in summary if k.endswith("_median")})
    for base in base_metrics:
        med = summary.get(f"{base}_median", 0.0)
        mn = summary.get(f"{base}_min", 0.0)
        mx = summary.get(f"{base}_max", 0.0)
        lines.append(f"| `{base}` | {med:.3f} | {mn:.3f} | {mx:.3f} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run unified USN benchmark suite")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--output-dir", default="benchmarks/reports/latest")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--market-duration", type=int, default=8)
    parser.add_argument("--market-rate", type=int, default=200)
    parser.add_argument("--order-duration", type=int, default=8)
    parser.add_argument("--order-qps", type=int, default=300)
    parser.add_argument("--timeout-s", type=int, default=120)
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()
    out_dir = Path(args.output_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    mpsc_bin = build_dir / "benchmarks" / "usn_mpsc_queue_bench"
    market_bin = build_dir / "apps" / "run_sim_market"
    order_bin = build_dir / "apps" / "run_sim_order"
    for p in (mpsc_bin, market_bin, order_bin):
        if not p.exists():
            raise FileNotFoundError(f"Missing benchmark binary: {p}")

    all_raw: Dict[str, List[str]] = {"mpsc": [], "market": [], "order": []}
    parsed_rows: Dict[str, List[Dict[str, float]]] = {"mpsc": [], "market": [], "order": []}

    repo_root = build_dir.parent

    for _ in range(args.runs):
        out = run_cmd([str(mpsc_bin)], args.timeout_s, cwd=build_dir)
        all_raw["mpsc"].append(out)
        parsed_rows["mpsc"].append(parse_mpsc_metrics(out))

        out = run_cmd(
            [
                str(market_bin),
                "--duration", str(args.market_duration),
                "--rate", str(args.market_rate),
                "--payload-size", "128",
                "--subscribers", "2",
                "--streams", "2",
                "--drop-rate", "0.01",
                "--reorder-rate", "0.01",
            ],
            args.timeout_s,
            cwd=repo_root,
        )
        all_raw["market"].append(out)
        row = parse_line_by_prefix(out, "[run_market_sim][summary]")
        parsed_rows["market"].append(row)

        out = run_cmd(
            [
                str(order_bin),
                "--duration", str(args.order_duration),
                "--qps", str(args.order_qps),
                "--burst", "4",
                "--cancel-ratio", "20",
                "--replace-ratio", "20",
            ],
            args.timeout_s,
            cwd=repo_root,
        )
        all_raw["order"].append(out)
        row = parse_line_by_prefix(out, "[order_client][final]")
        if not row:
            row = parse_line_by_prefix(out, "[order_client][metrics]")
        parsed_rows["order"].append(row)

    for k in ("mpsc", "market", "order"):
        write_csv(out_dir / f"{k}_runs.csv", parsed_rows[k])
        (out_dir / f"{k}_raw.log").write_text("\n\n===== RUN SPLIT =====\n\n".join(all_raw[k]), encoding="utf-8")

    summary = {k: summarize_numeric_rows(v) for k, v in parsed_rows.items()}
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")

    ts = dt.datetime.now(dt.UTC).strftime("%Y-%m-%d %H:%M:%S UTC")
    report = [
        "# USN Benchmark Suite Report",
        "",
        f"- generated_at: {ts}",
        f"- runs_per_case: {args.runs}",
        f"- build_dir: `{build_dir}`",
        "",
        "## Scenarios",
        "",
        "- `mpsc`: queue push/pop micro benchmark",
        "- `market`: UDP market sim summary (`[run_market_sim][summary]`)",
        "- `order`: TCP order client final metrics (`[order_client][final]`)",
        "",
        format_table("MPSC Queue", summary["mpsc"]),
        format_table("Market UDP", summary["market"]),
        format_table("Order TCP", summary["order"]),
        "## Artifacts",
        "",
        "- `mpsc_runs.csv`, `market_runs.csv`, `order_runs.csv`",
        "- `mpsc_raw.log`, `market_raw.log`, `order_raw.log`",
        "- `summary.json`",
        "",
    ]
    (out_dir / "report.md").write_text("\n".join(report), encoding="utf-8")
    print(f"[benchmark-suite] report written: {out_dir / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
