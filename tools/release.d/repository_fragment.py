#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Render the repository fragment of a release manifest as JSON.

Authority boundary: this module turns the values collect.sh read out of the
repository into the manifest's shape. The one additional repository document
it reads is the generated product-capability projection: its byte digest and
selected profile are release identity, not a build-job assertion. Pin parsing
still lives only in tools/lib, byte hashing only in repo_facts.py, and manifest
shape only here.

Owning milestone: M1 (WP-M1-07).

A value the repository could not determine is omitted, never defaulted. An
absent field fails validation with the schema's own remediation, which names
the job that has to supply it; a defaulted field would pass validation while
describing nothing.
"""

from __future__ import annotations

import datetime
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import repo_facts  # noqa: E402  (after sys.path setup)


def value(name: str) -> str:
    return os.environ.get(name, "").strip()


def integer(name: str):
    raw = value(name)
    try:
        return int(raw)
    except ValueError:
        return None


def rows(name: str, width: int):
    for line in value(name).splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) == width:
            yield parts


def prune(node):
    """Drop empty branches. An omitted field is a question the schema asks the
    next job; a field present and empty is an answer nobody gave."""
    if isinstance(node, dict):
        cleaned = {k: prune(v) for k, v in node.items()}
        return {k: v for k, v in cleaned.items() if v not in (None, "", [], {})}
    if isinstance(node, list):
        return [prune(item) for item in node if item not in (None, "", [], {})]
    return node


def timestamp() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    moment = (
        datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
        if epoch and epoch.isdigit()
        else datetime.datetime.now(datetime.timezone.utc)
    )
    return moment.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def capability_identity(root: str, build_profile: str) -> dict:
    """Return the capability identity selected by one committed GN profile.

    A missing or malformed projection produces no guessed identity. Candidate
    validation then names the generator output that is absent or unusable.
    """
    path = os.path.join(
        root, "taffy-core", "build", "capabilities", "generated",
        "product-capabilities.json",
    )
    try:
        with open(path, encoding="utf-8") as handle:
            projection = json.load(handle)
    except (OSError, json.JSONDecodeError):
        return {}
    if not isinstance(projection, dict):
        return {}
    chromium_profiles = projection.get("chromium_profiles")
    profiles = projection.get("profiles")
    if not isinstance(chromium_profiles, dict) or not isinstance(profiles, dict):
        return {}
    profile_name = chromium_profiles.get(build_profile)
    if not isinstance(profile_name, str) or not profile_name:
        return {}
    profile = profiles.get(profile_name) or {}
    if not isinstance(profile, dict):
        return {}
    return {
        "name": profile_name,
        "projection_sha256": repo_facts.sha256_file(path),
        "configuration_fingerprint": projection.get("configuration_fingerprint"),
        "accepted_milestone": projection.get("accepted_milestone"),
        "delegated_task_start": profile.get("delegated_task_start"),
        "task_milestone": profile.get("task_milestone"),
        "policy_milestone": profile.get("policy_milestone"),
    }


def build() -> dict:
    root = value("R_ROOT")
    advisories = value("R_ADVISORIES")
    counts = repo_facts.patch_counts(root)
    args_file = os.path.join(root, "chromium", "args", f'{value("R_PROFILE")}.gn')
    args_digest = repo_facts.sha256_file(args_file) if os.path.exists(args_file) else ""
    fragment = {
        "schema_version": 2,
        "generated_at": timestamp(),
        "generator": {
            "name": "tools/release",
            "command": value("R_COMMAND"),
            "host": f'{value("R_HOST_OS")}/{value("R_HOST_ARCH")}',
        },
        "release": {"kind": value("R_KIND")},
        "source": {
            "taffy": {
                "revision": value("R_REVISION"),
                "dirty": value("R_DIRTY") == "true",
                "branch": value("R_BRANCH"),
                "remote": value("R_REMOTE"),
            }
        },
        "chromium": {
            "milestone": value("R_CHROMIUM_MILESTONE"),
            "tag": value("R_CHROMIUM_TAG"),
            "commit": value("R_CHROMIUM_COMMIT"),
            "security_patch_level": integer("R_SPL"),
            "security_advisories": (
                [] if advisories in ("", "none") else [a.strip() for a in advisories.split(",")]
            ),
            "patch_queue": {
                "count": counts["count"],
                "modified_upstream_lines": counts["modified_upstream_lines"],
                "security_count": counts["security_count"],
                "digest": repo_facts.patch_queue_digest(root),
            },
            "depot_tools_revision": value("R_DEPOT_TOOLS"),
        },
        "build": {
            "profile": value("R_PROFILE"),
            "args_digest": args_digest,
            "host": {
                "os": value("R_HOST_OS"),
                "arch": value("R_HOST_ARCH"),
                "runner": value("R_RUNNER"),
            },
        },
        "toolchain": [
            {"component": component, "version": version, "owner_file": owner}
            for component, version, owner in rows("R_TOOLCHAIN", 3)
        ],
        "platform": {
            "min_sdk": integer("R_MIN_SDK"),
            "target_sdk": integer("R_TARGET_SDK"),
            "compile_sdk": integer("R_COMPILE_SDK"),
            "abis": [abi for abi in value("R_ABIS").split() if abi],
        },
        "dependencies": {"lock_digests": repo_facts.lock_digests(root)},
        "identity": {
            "capability_profile": capability_identity(root, value("R_PROFILE")),
        },
        "provenance": {
            "predicate_type": value("R_PREDICATE"),
            "builder_id": value("R_BUILDER"),
            "invocation": {
                "workflow": value("R_WORKFLOW"),
                "repository": value("R_PROV_REPO"),
                "ref": value("R_PROV_REF"),
                "sha": value("R_REVISION"),
                "run_id": value("R_RUN_ID"),
                "run_attempt": value("R_RUN_ATTEMPT"),
            },
        },
    }
    # security_advisories is legitimately empty and must survive the prune, so
    # it is restored afterwards rather than special-cased inside it.
    advisory_list = fragment["chromium"]["security_advisories"]
    patch_queue = fragment["chromium"]["patch_queue"]
    cleaned = prune(fragment)
    # An empty patch queue is a fact, not an absence: zero patches and zero
    # modified lines is what a clean fork looks like, and the prune above would
    # otherwise delete the evidence that it was measured.
    cleaned.setdefault("chromium", {})["security_advisories"] = advisory_list
    cleaned["chromium"]["patch_queue"] = patch_queue
    return cleaned


def main() -> int:
    json.dump(build(), sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
