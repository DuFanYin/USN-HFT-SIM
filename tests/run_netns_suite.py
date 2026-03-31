#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import time
from pathlib import Path
from typing import Dict, List


KV_RE = re.compile(r"([a-zA-Z0-9_]+)=([^\s]+)")


def parse_kv_line(line: str) -> Dict[str, float]:
    out: Dict[str, float] = {}
    for k, v in KV_RE.findall(line):
        try:
            out[k] = float(v)
        except ValueError:
            continue
    return out


def run(cmd: List[str]) -> None:
    subprocess.run(cmd, check=True)


def terminate_proc(proc: subprocess.Popen) -> None:
    if proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


def ns_exec(ns: str, cmd: List[str]) -> List[str]:
    return ["ip", "netns", "exec", ns] + cmd


def setup_netns(ns_client: str, ns_server: str) -> None:
    run(["ip", "netns", "add", ns_client])
    run(["ip", "netns", "add", ns_server])
    run(["ip", "link", "add", "veth-usn-c", "type", "veth", "peer", "name", "veth-usn-s"])
    run(["ip", "link", "set", "veth-usn-c", "netns", ns_client])
    run(["ip", "link", "set", "veth-usn-s", "netns", ns_server])
    run(["ip", "-n", ns_client, "addr", "add", "10.23.0.1/24", "dev", "veth-usn-c"])
    run(["ip", "-n", ns_server, "addr", "add", "10.23.0.2/24", "dev", "veth-usn-s"])
    run(["ip", "-n", ns_client, "link", "set", "lo", "up"])
    run(["ip", "-n", ns_server, "link", "set", "lo", "up"])
    run(["ip", "-n", ns_client, "link", "set", "veth-usn-c", "up"])
    run(["ip", "-n", ns_server, "link", "set", "veth-usn-s", "up"])
    run(["ip", "-n", ns_client, "route", "add", "239.0.0.0/8", "dev", "veth-usn-c"])
    run(["ip", "-n", ns_server, "route", "add", "239.0.0.0/8", "dev", "veth-usn-s"])


def apply_netem(ns_client: str, ns_server: str) -> None:
    run(ns_exec(ns_client, ["tc", "qdisc", "replace", "dev", "veth-usn-c", "root", "netem", "delay", "2ms", "1ms", "loss", "1%", "reorder", "1%"]))
    run(ns_exec(ns_server, ["tc", "qdisc", "replace", "dev", "veth-usn-s", "root", "netem", "delay", "2ms", "1ms", "loss", "1%", "reorder", "1%"]))


def cleanup_netns(ns_client: str, ns_server: str) -> None:
    subprocess.run(["ip", "netns", "del", ns_client], check=False)
    subprocess.run(["ip", "netns", "del", ns_server], check=False)


def find_last_metrics(log_text: str, marker: str) -> Dict[str, float]:
    selected = ""
    for line in log_text.splitlines():
        if marker in line:
            selected = line
    return parse_kv_line(selected) if selected else {}


def main() -> int:
    parser = argparse.ArgumentParser(description="Run TCP/UDP integration scenarios in Linux netns")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--output-dir", default="tests/netns/reports/latest")
    parser.add_argument("--duration", type=int, default=6)
    args = parser.parse_args()
    if os.geteuid() != 0:
        raise SystemExit("This script requires root privileges (ip netns/tc).")

    build_dir = Path(args.build_dir).resolve()
    out_dir = Path(args.output_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    order_gateway = build_dir / "apps" / "order_gateway"
    order_client = build_dir / "apps" / "order_client"
    feed_pub = build_dir / "apps" / "feed_publisher"
    feed_sub = build_dir / "apps" / "feed_subscriber"
    for p in (order_gateway, order_client, feed_pub, feed_sub):
        if not p.exists():
            raise FileNotFoundError(f"Missing binary: {p}")

    ns_client = f"usn-c-{os.getpid()}"
    ns_server = f"usn-s-{os.getpid()}"
    procs: List[subprocess.Popen] = []
    summary: Dict[str, Dict[str, float]] = {"tcp": {}, "udp": {}}

    try:
        setup_netns(ns_client, ns_server)
        apply_netem(ns_client, ns_server)
        gw_log = out_dir / "tcp_gateway.log"
        cl_log = out_dir / "tcp_client.log"
        with gw_log.open("w", encoding="utf-8") as gwf, cl_log.open("w", encoding="utf-8") as clf:
            p_gw = subprocess.Popen(ns_exec(ns_server, [str(order_gateway)]), stdout=gwf, stderr=subprocess.STDOUT, text=True)
            procs.append(p_gw)
            time.sleep(0.4)
            p_cl = subprocess.Popen(
                ns_exec(ns_client, [str(order_client), "--gateway-ip", "10.23.0.2", "--qps", "120", "--burst", "2", "--timeout-ms", "800", "--cancel-ratio", "20", "--replace-ratio", "20"]),
                stdout=clf,
                stderr=subprocess.STDOUT,
                text=True,
            )
            procs.append(p_cl)
            time.sleep(max(2, args.duration))
            terminate_proc(p_cl)
            terminate_proc(p_gw)
        summary["tcp"] = find_last_metrics(cl_log.read_text(encoding="utf-8"), "[order_client][final]")

        sub_log = out_dir / "udp_subscriber.log"
        pub_log = out_dir / "udp_publisher.log"
        sub_summary = out_dir / "udp_summary.txt"
        with sub_log.open("w", encoding="utf-8") as sf, pub_log.open("w", encoding="utf-8") as pf:
            p_sub = subprocess.Popen(ns_exec(ns_server, [str(feed_sub), "--subscriber-id", "0", "--summary-file", str(sub_summary)]), stdout=sf, stderr=subprocess.STDOUT, text=True)
            procs.append(p_sub)
            time.sleep(0.4)
            p_pub = subprocess.Popen(ns_exec(ns_client, [str(feed_pub), "--rate", "200", "--streams", "2", "--drop-rate", "0.01", "--reorder-rate", "0.01"]), stdout=pf, stderr=subprocess.STDOUT, text=True)
            procs.append(p_pub)
            time.sleep(max(2, args.duration))
            terminate_proc(p_pub)
            terminate_proc(p_sub)
        if sub_summary.exists():
            summary["udp"] = parse_kv_line(sub_summary.read_text(encoding="utf-8").strip())

        (out_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
        (out_dir / "report.md").write_text(
            "\n".join(
                [
                    "# netns Integration Report",
                    "",
                    f"- tcp_req_sent_total: {summary['tcp'].get('req_sent_total', 0)}",
                    f"- tcp_ack_recv_total: {summary['tcp'].get('ack_recv_total', 0)}",
                    f"- udp_recv_total: {summary['udp'].get('recv_total', 0)}",
                    f"- udp_gaps: {summary['udp'].get('gaps', 0)}",
                    "",
                    "## Pass Criteria",
                    "",
                    f"- TCP ack_recv_total > 0: {'PASS' if summary['tcp'].get('ack_recv_total', 0) > 0 else 'FAIL'}",
                    f"- UDP recv_total > 0: {'PASS' if summary['udp'].get('recv_total', 0) > 0 else 'FAIL'}",
                    "",
                ]
            ),
            encoding="utf-8",
        )
        print(f"[netns-suite] report written: {out_dir / 'report.md'}")
        return 0
    finally:
        for p in procs:
            if p.poll() is None:
                terminate_proc(p)
        cleanup_netns(ns_client, ns_server)


if __name__ == "__main__":
    raise SystemExit(main())
