#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Read-only device and build facts required by a measured benchmark run."""

from __future__ import annotations

import hashlib
import os
import re
import subprocess
from typing import Any


class DeviceError(RuntimeError):
    pass


def _command(argv: list[str], timeout: int = 30) -> str:
    try:
        result = subprocess.run(
            argv,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise DeviceError(f"could not run {' '.join(argv)}: {error}") from error
    if result.returncode != 0:
        raise DeviceError(
            f"{' '.join(argv)} exited {result.returncode}: {result.stdout.strip()}"
        )
    return result.stdout.replace("\r\n", "\n")


def file_sha256(path: str) -> str:
    digest = hashlib.sha256()
    try:
        with open(path, "rb") as handle:
            while chunk := handle.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise DeviceError(f"cannot hash {path}: {error}") from error
    return digest.hexdigest()


class AdbDevice:
    def __init__(self, serial: str) -> None:
        if not serial or any(char.isspace() for char in serial):
            raise DeviceError("--device must name one adb serial")
        self.serial = serial

    def adb(self, *arguments: str, timeout: int = 30) -> str:
        return _command(["adb", "-s", self.serial, *arguments], timeout=timeout)

    def shell(self, *arguments: str, timeout: int = 30) -> str:
        return self.adb("shell", *arguments, timeout=timeout)

    def property(self, name: str) -> str:
        value = self.shell("getprop", name).strip()
        if not value:
            raise DeviceError(f"device property {name} is empty")
        return value

    def require_ready(self, profile: str) -> None:
        if self.adb("get-state").strip() != "device":
            raise DeviceError(f"adb serial {self.serial} is not ready")
        abi = self.property("ro.product.cpu.abilist")
        required = "arm64-v8a" if profile.endswith("arm64") else "x86_64"
        if required not in abi.split(","):
            raise DeviceError(f"profile {profile} requires {required}; device reports {abi}")
        power = self.shell("dumpsys", "power")
        policy = self.shell("dumpsys", "window", "policy")
        if "mWakefulness=Awake" not in power or "showing=false" not in policy:
            raise DeviceError("device screen is asleep or the keyguard is showing")

    def package(self, package_name: str) -> dict[str, Any]:
        dump = self.shell("dumpsys", "package", package_name)
        if "Unable to find package" in dump or "versionCode=" not in dump:
            raise DeviceError(f"required package {package_name} is not installed")
        code = re.search(r"versionCode=(\d+)", dump)
        name = re.search(r"versionName=([^\n]+)", dump)
        update = re.search(r"lastUpdateTime=([^\n]+)", dump)
        return {
            "package": package_name,
            "version_code": int(code.group(1)) if code else None,
            "version_name": name.group(1).strip() if name else "",
            "last_update_time": update.group(1).strip() if update else "",
            "debuggable": "DEBUGGABLE" in dump,
        }

    def thermal(self) -> dict[str, Any]:
        dump = self.shell("dumpsys", "thermalservice")
        status = re.search(r"^Thermal Status:\s*(\d+)\s*$", dump, re.MULTILINE)
        if not status:
            raise DeviceError("thermalservice did not report Thermal Status")
        temperatures: list[dict[str, Any]] = []
        current = dump.split("Current temperatures from HAL:", 1)
        section = current[1].split("Current cooling devices", 1)[0] if len(current) == 2 else dump
        pattern = re.compile(
            r"Temperature\{mValue=([-+0-9.]+), mType=(\d+), "
            r"mName=([^,}]+), mStatus=(\d+)\}"
        )
        for match in pattern.finditer(section):
            temperatures.append(
                {
                    "name": match.group(3),
                    "type": int(match.group(2)),
                    "celsius": float(match.group(1)),
                    "status": int(match.group(4)),
                }
            )
        if not temperatures:
            raise DeviceError("thermalservice reported no current HAL temperatures")
        return {"status": int(status.group(1)), "temperatures": temperatures}

    def battery(self) -> dict[str, Any]:
        dump = self.shell("dumpsys", "battery")
        values: dict[str, str] = {}
        for line in dump.splitlines():
            if ":" not in line:
                continue
            key, value = line.strip().split(":", 1)
            values[key] = value.strip()
        required = ("level", "scale", "temperature", "status")
        if any(key not in values for key in required):
            raise DeviceError("battery service omitted required measurement fields")
        return {
            "level_percent": 100 * int(values["level"]) / int(values["scale"]),
            "temperature_celsius": int(values["temperature"]) / 10.0,
            "status": int(values["status"]),
            "usb_powered": values.get("USB powered") == "true",
        }

    def facts(self) -> dict[str, Any]:
        return {
            "serial": self.serial,
            "manufacturer": self.property("ro.product.manufacturer"),
            "model": self.property("ro.product.model"),
            "device": self.property("ro.product.device"),
            "fingerprint": self.property("ro.build.fingerprint"),
            "sdk": int(self.property("ro.build.version.sdk")),
            "abis": self.property("ro.product.cpu.abilist").split(","),
        }


def build_facts(repo_root: str, chromium_src: str, profile: str) -> dict[str, Any]:
    out = os.path.join(chromium_src, "out", profile)
    paths = {
        "product_apk": os.path.join(out, "apks", "TaffyGo_incremental.apk"),
        "observation_runner": os.path.join(out, "bin", "run_taffy_browsertests"),
        "observation_apk": os.path.join(
            out, "taffy_browsertests_apk", "taffy_browsertests-debug_incremental.apk"
        ),
        "task_runner": os.path.join(out, "bin", "run_android_browsertests"),
        "task_apk": os.path.join(
            out, "android_browsertests_apk", "android_browsertests-debug_incremental.apk"
        ),
        "gn_args": os.path.join(repo_root, "chromium", "args", f"{profile}.gn"),
    }
    missing = [name for name, path in paths.items() if not os.path.isfile(path)]
    if missing:
        raise DeviceError(
            "required benchmark build outputs are absent: " + ", ".join(sorted(missing))
        )
    for runner in (paths["observation_runner"], paths["task_runner"]):
        if not os.access(runner, os.X_OK):
            raise DeviceError(f"benchmark runner is not executable: {runner}")
    revision = _command(["git", "-C", repo_root, "rev-parse", "HEAD"]).strip()
    status = _command(["git", "-C", repo_root, "status", "--porcelain=v1"])
    return {
        "profile": profile,
        "chromium_src": os.path.abspath(chromium_src),
        "git_revision": revision,
        "git_dirty_paths": len([line for line in status.splitlines() if line]),
        "git_status_sha256": hashlib.sha256(status.encode("utf-8")).hexdigest(),
        "artifacts": {
            name: {
                "path": os.path.relpath(path, chromium_src).replace(os.sep, "/"),
                "sha256": file_sha256(path),
                "size_bytes": os.path.getsize(path),
                "modified_epoch_ns": os.stat(path).st_mtime_ns,
            }
            for name, path in paths.items()
        },
    }
