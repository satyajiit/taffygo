#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Run benchmark browser-test adapters and recover their signed-off records."""

from __future__ import annotations

import base64
import json
import os
import re
import selectors
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Any

from device import AdbDevice, DeviceError
from score import PROTOCOL


MARKER = re.compile(r"TAFFY_BENCHMARK_RECORD_V1=([A-Za-z0-9+/=]+)")


class LauncherError(RuntimeError):
    pass


@dataclass(frozen=True)
class Component:
    kind: str
    suite: str
    fixture: str


COMPONENTS = (
    Component("observation", "taffy_browsertests", "TaskBenchmarkObservationBrowserTest"),
    Component("task", "android_browsertests", "TaskBenchmarkVerticalBrowserTest"),
)

def test_name(component: Component, scenario_id: str) -> str:
    return f"{component.fixture}.{scenario_id.replace('-', '_')}"


def _component_command(
    repo_root: str,
    profile: str,
    serial: str,
    component: Component,
    selected: list[str],
    summary_path: str,
) -> list[str]:
    filters = ":".join(test_name(component, scenario_id) for scenario_id in selected)
    return [
        os.path.join(repo_root, "tools", "chromium", "test"),
        "--profile",
        profile,
        "--device",
        serial,
        component.suite,
        "--",
        f"--gtest_filter={filters}",
        "--test-launcher-jobs=1",
        "--test-launcher-retry-limit=0",
        f"--test-launcher-summary-output={summary_path}",
    ]


def _stream_command(argv: list[str], log_path: str, timeout_seconds: int) -> tuple[int, int]:
    started = time.monotonic_ns()
    try:
        process = subprocess.Popen(
            argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            start_new_session=True,
        )
    except OSError as error:
        raise LauncherError(f"could not start {' '.join(argv)}: {error}") from error
    assert process.stdout is not None
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout_seconds
    with open(log_path, "w", encoding="utf-8") as log:
        while process.poll() is None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                raise LauncherError(
                    f"test launcher exceeded {timeout_seconds}s; partial log: {log_path}"
                )
            events = selector.select(timeout=min(1.0, remaining))
            for key, _ in events:
                line = key.fileobj.readline()
                if line:
                    log.write(line)
                    log.flush()
                    sys.stderr.write(line)
                    sys.stderr.flush()
        tail = process.stdout.read()
        if tail:
            log.write(tail)
            sys.stderr.write(tail)
    return process.returncode, time.monotonic_ns() - started


def _result_rows(summary: dict[str, Any]) -> dict[str, dict[str, Any]]:
    iterations = summary.get("per_iteration_data")
    if not isinstance(iterations, list) or len(iterations) != 1:
        raise LauncherError("launcher summary must contain exactly one iteration")
    rows = iterations[0]
    if not isinstance(rows, dict):
        raise LauncherError("launcher iteration is not an object")
    result: dict[str, dict[str, Any]] = {}
    for name, attempts in rows.items():
        if not isinstance(attempts, list) or len(attempts) != 1:
            raise LauncherError(f"{name} did not produce exactly one attempt")
        if not isinstance(attempts[0], dict):
            raise LauncherError(f"{name} attempt is not an object")
        result[name] = attempts[0]
    return result


def _decode_record(snippet: str, expected_kind: str, expected_id: str) -> dict[str, Any]:
    # A record is a machine-readable stdout line, not a token which may appear
    # inside a human log message. In particular, LOG(INFO) prepends metadata;
    # accepting an unanchored match there would let a logging side effect count
    # as benchmark evidence. splitlines() also normalizes CRLF without making
    # surrounding whitespace part of the wire format.
    matches = [
        match.group(1)
        for line in snippet.splitlines()
        if (match := MARKER.fullmatch(line)) is not None
    ]
    if len(matches) != 1:
        raise LauncherError(
            f"{expected_id} emitted {len(matches)} benchmark records; expected exactly one"
        )
    try:
        raw = base64.b64decode(matches[0], validate=True)
        record = json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise LauncherError(f"{expected_id} emitted an invalid benchmark record: {error}") from error
    if not isinstance(record, dict):
        raise LauncherError(f"{expected_id} benchmark record is not an object")
    if record.get("protocol") != PROTOCOL:
        raise LauncherError(f"{expected_id} emitted an unsupported benchmark protocol")
    if record.get("kind") != expected_kind or record.get("scenario_id") != expected_id:
        raise LauncherError(f"{expected_id} benchmark record names the wrong component or scenario")
    return record


def _self_test() -> int:
    record = {
        "protocol": PROTOCOL,
        "kind": "task",
        "scenario_id": "TB-101",
    }
    encoded = base64.b64encode(
        json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).decode("ascii")
    line = f"TAFFY_BENCHMARK_RECORD_V1={encoded}"

    decoded = _decode_record(f"{line}\r\n", "task", "TB-101")
    if decoded != record:
        raise AssertionError("raw benchmark record did not round-trip")

    refused = (
        f"[1:2:0904/120000.000000:INFO:adapter.cc(1)] {line}\n",
        f"prefix {line}\n",
        f"{line} suffix\n",
        f"{line}\n{line}\n",
    )
    for snippet in refused:
        try:
            _decode_record(snippet, "task", "TB-101")
        except LauncherError:
            continue
        raise AssertionError(f"launcher accepted a non-canonical record line: {snippet!r}")

    command = _component_command(
        "/repo",
        "dev-arm64",
        "device-1",
        COMPONENTS[0],
        ["TB-101"],
        "/tmp/summary.json",
    )
    required_options = {
        "--test-launcher-retry-limit=0",
        "--test-launcher-jobs=1",
    }
    if not required_options.issubset(command):
        raise AssertionError(f"benchmark launcher lost bounded single-attempt options: {command}")
    if any(option.startswith("--timeout-scale") for option in command):
        raise AssertionError(
            "Android browser tests must keep the default timeout scale and UI-thread runner"
        )

    print("benchmark launcher self-test: raw record and single-attempt runner rules hold")
    return 0


def install_test_packages(
    device: AdbDevice, chromium_src: str, profile: str
) -> list[dict[str, Any]]:
    """Install test APK identities before Chromium's incremental installer runs.

    This makes All-files access grantable before the launcher starts serving
    runtime data.  The launcher still performs its authoritative incremental
    install immediately afterwards.
    """
    out = os.path.join(chromium_src, "out", profile)
    entries = (
        (
            "com.taffygo.browsertests",
            os.path.join(out, "taffy_browsertests_apk", "taffy_browsertests-debug_incremental.apk"),
        ),
        (
            "org.chromium.android_browsertests_apk",
            os.path.join(out, "android_browsertests_apk", "android_browsertests-debug_incremental.apk"),
        ),
    )
    installed = []
    for package, apk in entries:
        output = device.adb("install", "-r", apk, timeout=180)
        if "Success" not in output:
            raise DeviceError(f"adb did not confirm installation of {package}: {output.strip()}")
        device.shell("appops", "set", package, "MANAGE_EXTERNAL_STORAGE", "allow")
        installed.append(device.package(package))
    return installed


def run_component(
    repo_root: str,
    profile: str,
    serial: str,
    component: Component,
    selected: list[str],
    raw_dir: str,
    sample: int,
    timeout_seconds: int,
) -> dict[str, Any]:
    os.makedirs(raw_dir, exist_ok=True)
    summary_path = os.path.join(raw_dir, f"{component.kind}-sample-{sample}.summary.json")
    log_path = os.path.join(raw_dir, f"{component.kind}-sample-{sample}.log")
    command = _component_command(
        repo_root, profile, serial, component, selected, summary_path
    )
    return_code, elapsed_ns = _stream_command(command, log_path, timeout_seconds)
    if return_code != 0:
        raise LauncherError(
            f"{component.kind} adapter exited {return_code}; retained log: {log_path}"
        )
    try:
        with open(summary_path, encoding="utf-8") as handle:
            summary = json.load(handle)
    except (OSError, ValueError) as error:
        raise LauncherError(f"cannot read launcher summary {summary_path}: {error}") from error
    rows = _result_rows(summary)
    records: dict[str, dict[str, Any]] = {}
    timings: dict[str, int] = {}
    for scenario_id in selected:
        name = test_name(component, scenario_id)
        row = rows.get(name)
        if row is None:
            raise LauncherError(f"launcher did not execute selected test {name}")
        if row.get("status") != "SUCCESS":
            raise LauncherError(f"selected test {name} reported {row.get('status')}")
        snippet = str(row.get("output_snippet", ""))
        records[scenario_id] = _decode_record(snippet, component.kind, scenario_id)
        elapsed = row.get("elapsed_time_ms")
        if not isinstance(elapsed, int) or elapsed < 0:
            raise LauncherError(f"selected test {name} has no valid elapsed time")
        timings[scenario_id] = elapsed
    unexpected = sorted(set(rows) - {test_name(component, item) for item in selected})
    if unexpected:
        raise LauncherError(f"launcher executed unselected tests: {', '.join(unexpected)}")
    return {
        "kind": component.kind,
        "suite": component.suite,
        "sample": sample,
        "elapsed_ns": elapsed_ns,
        "summary": os.path.basename(summary_path),
        "log": os.path.basename(log_path),
        "records": records,
        "test_elapsed_ms": timings,
    }


if __name__ == "__main__":
    if sys.argv[1:] != ["--self-test"]:
        print(f"usage: {sys.argv[0]} --self-test", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(_self_test())
