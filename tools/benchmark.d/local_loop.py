#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Measure local Rust interfaces without claiming browser or provider latency."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
MARKER = "TAFFY_LOCAL_LATENCY_V1="
WORKLOAD = "TAFFY_LOCAL_WORKLOAD_V1="
TEST = "composition::profile::tests::local_latency::measure_local_loop_interfaces"
CASES = {
    "timer_control", "tools_construct_and_discover", "tools_discover_resident",
    "memory_candidates_cold_512", "memory_candidates_warm_512",
    "schedule_1_source_reads", "schedule_4_source_reads", "schedule_fifth_source_waits",
}


def command(args: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=True, **kwargs)


def bounded_count(value: str) -> int:
    count = int(value)
    if not 1 <= count <= 10_000:
        raise argparse.ArgumentTypeError("use a count between 1 and 10000")
    return count


def build() -> Path:
    print("Building the release-mode host measurement executable", flush=True)
    result = command([
        "cargo", "test", "-p", "core-runtime", "--release", "--locked", "--lib",
        "--no-run", "--message-format=json",
    ])
    artifacts = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
    paths = [item["executable"] for item in artifacts
             if item.get("reason") == "compiler-artifact" and item.get("executable")
             and item.get("target", {}).get("name") == "core_runtime"]
    if len(paths) != 1:
        raise ValueError("Cargo did not identify exactly one core-runtime test executable")
    return Path(paths[0])


def parse_run(output: str, samples: int, iterations: int) -> list[dict]:
    rows = [json.loads(line.removeprefix(MARKER)) for line in output.splitlines()
            if line.startswith(MARKER)]
    if len(rows) != len(CASES) or {row.get("case") for row in rows} != CASES:
        raise ValueError("measurement omitted, duplicated or added an unknown workload")
    for row in rows:
        values = row.get("batch_totals_ns", [])
        if (row.get("samples") != samples or row.get("iterations") != iterations
                or len(values) != samples
                or any(type(value) is not int or value <= 0 for value in values)):
            raise ValueError("measurement has invalid sample counts or elapsed times")
    return rows


def summaries(trials: list[list[dict]]) -> list[dict]:
    result = []
    for name in sorted(CASES):
        samples = sorted(value / row["iterations"] / 1000
                         for trial in trials for row in trial if row["case"] == name
                         for value in row["batch_totals_ns"])
        percentile = lambda quantile: samples[math.ceil(len(samples) * quantile) - 1]
        result.append({
            "case": name, "batch_samples": len(samples),
            "batch_mean_us_p50": statistics.median(samples),
            "batch_mean_us_p95": percentile(0.95),
            "batch_mean_us_p99": percentile(0.99),
            "batch_mean_us_min": samples[0], "batch_mean_us_max": samples[-1],
        })
    return result


def cpu_model() -> str:
    path = Path("/proc/cpuinfo")
    if path.is_file():
        return next((line.split(":", 1)[1].strip() for line in path.read_text().splitlines()
                     if line.startswith("model name")), platform.processor())
    return platform.processor()


def load_state(cpu: int | None) -> dict:
    frequency = None
    governor = None
    if cpu is not None:
        root = Path(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq")
        if (root / "scaling_cur_freq").is_file():
            frequency = int((root / "scaling_cur_freq").read_text().strip())
        if (root / "scaling_governor").is_file():
            governor = (root / "scaling_governor").read_text().strip()
    return {"load_average": os.getloadavg(), "cpu_frequency_khz": frequency,
            "cpu_governor": governor}


def self_test() -> None:
    rows = [{"case": name, "samples": 2, "iterations": 1, "batch_totals_ns": [100, 200]}
            for name in sorted(CASES)]
    render = lambda items: "\n".join(MARKER + json.dumps(row) for row in items)
    assert parse_run(render(rows), 2, 1) == rows
    for invalid in [rows[:-1], rows + rows[:1],
                    [dict(row, batch_totals_ns=[0, 100]) for row in rows],
                    [dict(row, iterations=2) for row in rows]]:
        try:
            parse_run(render(invalid), 2, 1)
        except ValueError:
            continue
        raise ValueError("measurement parser accepted incomplete or invalid evidence")
    assert all(math.isclose(row["batch_mean_us_p50"], 0.15) for row in summaries([rows]))
    print("Local measurement parser self-test passed; no latency was measured")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="write measured JSON and raw log")
    parser.add_argument("--self-test", action="store_true", help="validate evidence parsing only")
    parser.add_argument("--cpu", type=int, help="pin only the measurement process to this allowed CPU")
    parser.add_argument("--trials", type=bounded_count, default=3)
    parser.add_argument("--samples", type=bounded_count, default=101)
    parser.add_argument("--iterations", type=bounded_count, default=50)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.output is None:
        parser.error("--output is required for a measured run")
    affinity = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
    if args.cpu is not None and (affinity is None or args.cpu not in affinity):
        parser.error("--cpu must name a CPU in this process's available affinity")
    executable = build()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    log = args.output.with_suffix(".log")
    environment = dict(os.environ, TAFFY_LOCAL_LATENCY_SAMPLES=str(args.samples),
                       TAFFY_LOCAL_LATENCY_ITERATIONS=str(args.iterations))
    trials = []
    workload = None
    before = load_state(args.cpu)
    with log.open("w") as raw:
        for trial in range(args.trials):
            print(f"Measuring local interfaces: trial {trial + 1}/{args.trials}", flush=True)
            options = {}
            if args.cpu is not None:
                options["preexec_fn"] = lambda: os.sched_setaffinity(0, {args.cpu})
            measured = command([str(executable), "--exact", TEST, "--ignored", "--nocapture",
                                "--test-threads=1"], env=environment, **options)
            raw.write(measured.stdout)
            raw.write(measured.stderr)
            dimensions = [json.loads(line.removeprefix(WORKLOAD))
                          for line in measured.stdout.splitlines() if line.startswith(WORKLOAD)]
            if (len(dimensions) != 3 or {row.get("name") for row in dimensions}
                    != {"tools", "memory", "scheduling"}):
                raise ValueError("measurement omitted workload dimensions")
            if workload is not None and workload != dimensions:
                raise ValueError("workload changed between trials")
            workload = dimensions
            trials.append(parse_run(measured.stdout, args.samples, args.iterations))
    report = {
        "schema_version": 1, "measured": True, "kind": "local-interface-latency",
        "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
        "git_head": command(["git", "rev-parse", "HEAD"]).stdout.strip(),
        "working_tree_dirty": bool(command(["git", "status", "--porcelain"]).stdout),
        "test_executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "raw_log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
        "host": {"platform": platform.platform(), "cpu_model": cpu_model(),
                 "logical_cpus": os.cpu_count(), "available_affinity": affinity,
                 "measurement_affinity": [args.cpu] if args.cpu is not None else affinity,
                 "before": before, "after": load_state(args.cpu)},
        "compiler": command(["rustc", "--version", "--verbose"]).stdout.strip(),
        "profile": "cargo release; default target; no custom RUSTFLAGS required",
        "rustflags": environment.get("RUSTFLAGS", ""),
        "sampling": {"trials": args.trials, "samples_per_trial": args.samples,
                     "calls_per_sample": args.iterations, "clock": "std::time::Instant",
                     "percentiles": "distribution of per-batch mean interface call time"},
        "excluded": ["fixture setup", "returned-value destruction", "browser IPC",
                     "durable disk writes", "page/network wait", "provider/model wait"],
        "cache_cold_means": "empty application candidate cache; CPU/page caches uncontrolled",
        "workloads": workload, "summary": summaries(trials), "trials": trials,
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    for row in report["summary"]:
        print(f"{row['case']}: p50 {row['batch_mean_us_p50']:.3f} us, "
              f"p95 {row['batch_mean_us_p95']:.3f} us")
    print(f"Measured report: {args.output}\nRaw log: {log}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout or "", error.stderr or "", file=sys.stderr)
        raise SystemExit(error.returncode) from error
    except (OSError, ValueError) as error:
        raise SystemExit(f"Local measurement failed: {error}") from error
