#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Verify a release artifact manifest end to end.

Authority boundary: this module chooses the profile, runs every check in order,
and prints findings a human can act on. The checks themselves are
verify_checks.py; the rules they enforce belong to the documents named there.

Owning milestone: M1 (WP-M1-07).

  MANIFEST                 the manifest to verify
  --root DIR               repository to compare against (default: this one)
  --artifacts-dir DIR      where the listed artifacts are (default: the
                           manifest's own directory)
  --profile KIND           override the manifest's release.kind
  --require-approvals      also require approvals for development/drill; a
                           candidate always requires them
  --json                   machine-readable findings

Every failure names the exact item and the next step. Exit status: 0 verified,
1 findings, 2 the manifest could not be read.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import verify_artifacts  # noqa: E402
import verify_checks as checks  # noqa: E402  (after sys.path setup)
import promotion_policy  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    parser = argparse.ArgumentParser(prog="verify.py", description=__doc__.splitlines()[0])
    parser.add_argument("manifest")
    parser.add_argument("--root", default=os.path.abspath(os.path.join(HERE, "..", "..")))
    parser.add_argument("--artifacts-dir")
    parser.add_argument("--profile", choices=("development", "drill", "candidate"))
    parser.add_argument("--require-approvals", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        with open(args.manifest, encoding="utf-8") as handle:
            manifest = json.load(handle)
    except FileNotFoundError:
        print(f"{args.manifest}: no such manifest.", file=sys.stderr)
        print("    fix: Produce it with ./tools/release manifest --out <dir>.",
              file=sys.stderr)
        return 2
    except json.JSONDecodeError as error:
        print(f"{args.manifest}: is not valid JSON ({error}).", file=sys.stderr)
        return 2

    profile = args.profile or checks.get(manifest, "release", "kind", default="development")
    artifacts_dir = args.artifacts_dir or os.path.dirname(os.path.abspath(args.manifest))

    report = checks.Report()
    checks.check_schema(manifest, profile, report)
    checks.check_repository_agreement(manifest, args.root, report)
    checks.check_build_configuration(manifest, profile, report)
    verify_artifacts.check_artifacts(manifest, artifacts_dir, profile, report)
    verify_artifacts.check_dependencies(manifest, artifacts_dir, profile, report)
    checks.check_signing(manifest, profile, report)
    checks.check_provenance(manifest, artifacts_dir, profile, report)
    checks.check_toolchain(manifest, profile, report)
    checks.check_drill(manifest, artifacts_dir, profile, report)
    promotion_policy.check(
        manifest, profile, args.root, artifacts_dir, report,
        require_approvals=args.require_approvals,
    )

    if args.json:
        json.dump({"manifest": args.manifest, "profile": profile,
                   "ok": report.failures == 0, "findings": report.findings},
                  sys.stdout, indent=2)
        sys.stdout.write("\n")
        return 1 if report.failures else 0

    for finding in report.findings:
        marker = "fail" if finding["severity"] == "fail" else "warn"
        print(f"  {marker}  {finding['item']}: {finding['message']}")
        print(f"        fix: {finding['remediation']}")
    warnings = len(report.findings) - report.failures
    print()
    print(f"  {report.checks} check group(s), {report.failures} failure(s), "
          f"{warnings} warning(s) — profile \"{profile}\"")
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main())
