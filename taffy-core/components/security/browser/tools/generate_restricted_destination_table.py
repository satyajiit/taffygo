#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Validate the shared destination-class table and generate its C++ view."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import tempfile


TAFFY_ROOT = pathlib.Path(__file__).resolve().parents[4]
DEFAULT_INPUT = TAFFY_ROOT / "common/policy/restricted_destination_classes.tsv"
VALID_CLASSES = {
    "admin_console",
    "banking",
    "cloud_storage",
    "crypto",
    "email",
    "government",
    "health",
    "password_manager",
    "payments",
}
HOST = re.compile(
    r"(?=.{1,253}\Z)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+"
    r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?"
)


class Failure(Exception):
    """A table that cannot safely become a compiled policy."""


def read_hosts(path: pathlib.Path) -> list[str]:
    hosts: list[str] = []
    seen: set[str] = set()
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 3:
            raise Failure(f"{path}:{number}: expected three tab-separated fields")
        host, site_class, reason = fields
        if not HOST.fullmatch(host) or host.endswith(".test"):
            raise Failure(f"{path}:{number}: invalid shipping host {host!r}")
        if site_class not in VALID_CLASSES:
            raise Failure(f"{path}:{number}: unknown destination class {site_class!r}")
        if not reason.strip():
            raise Failure(f"{path}:{number}: empty review reason")
        if host in seen:
            raise Failure(f"{path}:{number}: duplicate host {host!r}")
        seen.add(host)
        hosts.append(host)
    if not hosts:
        raise Failure(f"{path}: shipping table is empty")
    return sorted(hosts)


def render(hosts: list[str]) -> str:
    rows = "\n".join(f'    "{host}",' for host in hosts)
    return f"""// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// GENERATED FILE - DO NOT EDIT AND DO NOT COMMIT.
// Source: //taffy/common/policy/restricted_destination_classes.tsv.

#ifndef TAFFY_COMPONENTS_SECURITY_BROWSER_RESTRICTED_DESTINATION_TABLE_GENERATED_H_
#define TAFFY_COMPONENTS_SECURITY_BROWSER_RESTRICTED_DESTINATION_TABLE_GENERATED_H_

#include <array>
#include <string_view>

namespace taffy::destination_class {{

inline constexpr std::array<std::string_view, {len(hosts)}> kRestrictedHosts = {{
{rows}
}};

}}  // namespace taffy::destination_class

#endif  // TAFFY_COMPONENTS_SECURITY_BROWSER_RESTRICTED_DESTINATION_TABLE_GENERATED_H_
"""


def self_test() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory) / "table.tsv"
        path.write_text(
            "mail.example\temail\treviewed\nfiles.example\tcloud_storage\treviewed\n",
            encoding="utf-8",
        )
        if read_hosts(path) != ["files.example", "mail.example"]:
            raise Failure("self-test: valid rows were not sorted")
        bad_rows = (
            "UPPER.example\temail\treviewed\n",
            "mail.example\tunknown\treviewed\n",
            "mail.example\temail\t\n",
            "fixture.test\temail\treviewed\n",
            "mail.example\temail\treviewed\nmail.example\temail\tagain\n",
        )
        for row in bad_rows:
            path.write_text(row, encoding="utf-8")
            try:
                read_hosts(path)
            except Failure:
                continue
            raise Failure(f"self-test: accepted invalid row {row!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=pathlib.Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=pathlib.Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--generate", action="store_true")
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            print("restricted destination table generator self-test: PASS")
            return 0
        hosts = read_hosts(args.input)
        if args.check:
            print(f"restricted destination table: {len(hosts)} reviewed hosts")
            return 0
        if args.output is None:
            raise Failure("--generate requires --output")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(render(hosts), encoding="utf-8")
        return 0
    except (Failure, OSError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
