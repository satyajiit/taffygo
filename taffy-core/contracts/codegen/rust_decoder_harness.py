# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Run the generated Rust decoders against a committed compatibility corpus.

A compatibility corpus that only a checker reads proves the checker. The point
of the corpus is what the code that ships does with those bytes, so this module
compiles the committed generated Rust — the same files the product crates
`#[path]` into themselves — into a throwaway binary and asks it. There is no
Cargo project and no dependency: the generated sources use nothing but `core`
and `std`, so one `rustc` invocation is the whole build, and it neither touches
nor waits on the workspace's target directory.

`rustc` is not required. The contracts lane's readiness predicate promises the
generator runs on any host with `python3`, and that promise is kept: when
`rustc` is absent this returns `None` and the caller reports, in as many words,
that the generated decoder was not run on this host. It never reports a pass
for something it did not execute.

Kotlin is a different matter and the answer is plainly no. The Kotlin codec is
generated and committed beside the Rust one, but running it needs a Kotlin
compiler and a JVM, neither of which a stdlib-only Python generator may assume
or install. The Kotlin decoder is therefore **not** proven by this gate.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from contract_schema import CONTRACT_ROOT, ContractError

#: Where the harness's answers come from. `payload` decodes committed bytes,
#: `enum` asks a closed enumeration whether a wire value is a member, and
#: `limit` asks a generated bound whether a measured size is within it.
JOB_KINDS = ("payload", "enum", "limit")


def available() -> bool:
    return shutil.which("rustc") is not None


def _module_lines(sources: list[Path]) -> list[str]:
    lines: list[str] = []
    for index, source in enumerate(sources):
        lines += [
            "#[allow(dead_code, unused_imports)]",
            f'#[path = "{source}"]',
            f"mod generated{index};",
            f"pub use generated{index}::*;",
        ]
    return lines


def _dispatch_lines(codecs: dict[str, str], enums: list[str], limits: list[str]) -> list[str]:
    lines = [
        "fn payload(codec: &str, bytes: &[u8]) -> String {",
        "    match codec {",
    ]
    for name, function in sorted(codecs.items()):
        lines += [
            f'        "{name}" => match {function}(bytes) {{',
            '            Ok(_) => "ACCEPT".to_owned(),',
            '            Err(error) => format!("{error:?}"),',
            "        },",
        ]
    lines += [
        '        _ => "UNKNOWN_CODEC".to_owned(),',
        "    }",
        "}",
        "",
        "fn closed_enum(name: &str, wire: u32) -> String {",
        "    let known = match name {",
    ]
    for name in sorted(enums):
        lines.append(f'        "{name}" => {name}::from_wire(wire).is_some(),')
    lines += [
        '        _ => return "UNKNOWN_ENUM".to_owned(),',
        "    };",
        '    if known { "KNOWN".to_owned() } else { "UNSUPPORTED".to_owned() }',
        "}",
        "",
        "fn bound(name: &str, measured: u64) -> String {",
        "    let limit = match name {",
    ]
    for name in sorted(limits):
        lines.append(f'        "{name}" => {name} as u64,')
    lines += [
        '        _ => return "UNKNOWN_LIMIT".to_owned(),',
        "    };",
        '    if measured > limit { "OVER".to_owned() } else { "WITHIN".to_owned() }',
        "}",
    ]
    return lines


def _main_lines() -> list[str]:
    return [
        "fn decode_hex(text: &str) -> Vec<u8> {",
        "    let digits: Vec<u8> = text.bytes().filter(|byte| !byte.is_ascii_whitespace()).collect();",
        "    digits",
        "        .chunks(2)",
        "        .filter_map(|pair| std::str::from_utf8(pair).ok())",
        "        .filter_map(|text| u8::from_str_radix(text, 16).ok())",
        "        .collect()",
        "}",
        "",
        "fn main() {",
        "    let path = std::env::args().nth(1).unwrap_or_default();",
        "    let jobs = std::fs::read_to_string(&path).unwrap_or_default();",
        "    for line in jobs.lines() {",
        "        let parts: Vec<&str> = line.split('\\t').collect();",
        "        let answer = match parts.as_slice() {",
        "            [\"payload\", codec, file] => {",
        "                let text = std::fs::read_to_string(file).unwrap_or_default();",
        "                payload(codec, &decode_hex(&text))",
        "            }",
        "            [\"enum\", name, wire] => closed_enum(name, wire.parse().unwrap_or(u32::MAX)),",
        "            [\"limit\", name, measured] => bound(name, measured.parse().unwrap_or(u64::MAX)),",
        "            _ => \"UNREADABLE_JOB\".to_owned(),",
        "        };",
        "        println!(\"{answer}\");",
        "    }",
        "}",
    ]


def render_harness(
    sources: list[Path], codecs: dict[str, str], enums: list[str], limits: list[str]
) -> str:
    lines = ["#![allow(unused)]", ""]
    lines += _module_lines(sources)
    lines += [""]
    lines += _dispatch_lines(codecs, enums, limits)
    lines += [""]
    lines += _main_lines()
    return "\n".join(lines) + "\n"


def run(
    sources: list[Path],
    codecs: dict[str, str],
    enums: list[str],
    limits: list[str],
    jobs: list[tuple[str, str, str]],
) -> list[str] | None:
    """The generated decoder's answer to each job, or `None` without `rustc`."""
    if not available():
        return None
    if not jobs:
        return []
    with tempfile.TemporaryDirectory(prefix="taffy-contract-decoder-") as directory:
        workspace = Path(directory)
        main = workspace / "main.rs"
        main.write_text(render_harness(sources, codecs, enums, limits), encoding="utf-8")
        binary = workspace / "harness"
        build = subprocess.run(
            ["rustc", "--edition", "2021", "--crate-name", "taffy_contract_decoder",
             "-C", "opt-level=0", "-o", str(binary), str(main)],
            capture_output=True,
            text=True,
            check=False,
        )
        if build.returncode != 0:
            raise ContractError(
                "the generated Rust decoders do not compile on their own:\n"
                + build.stderr.strip()
            )
        job_file = workspace / "jobs.tsv"
        job_file.write_text(
            "".join("\t".join(job) + "\n" for job in jobs), encoding="utf-8"
        )
        answered = subprocess.run(
            [str(binary), str(job_file)], capture_output=True, text=True, check=False
        )
        if answered.returncode != 0:
            raise ContractError(
                "the generated Rust decoder harness did not run:\n" + answered.stderr.strip()
            )
        lines = answered.stdout.splitlines()
    if len(lines) != len(jobs):
        raise ContractError(
            f"the generated Rust decoder answered {len(lines)} of {len(jobs)} jobs"
        )
    return lines


def generated_sources(contract: str, names: list[str]) -> list[Path]:
    root = CONTRACT_ROOT / contract / "generated" / "rust"
    return [root / name for name in names]


def enum_names(schema: dict[str, Any]) -> list[str]:
    names = [item["name"] for item in schema["enums"]]
    for codec in schema.get("binary_codecs", []):
        names += [item["name"] for item in codec["types"]["enums"]]
    return names


def limit_names(schema: dict[str, Any]) -> list[str]:
    return sorted(schema["limits"])
