#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Authoritative locations of the first-party Rust crates compiled by //taffy.

The hard cutover keeps every first-party crate in its owning component under
``taffy-core``.  Nothing is copied or symlink-mounted from the repository-level
``crates`` directory.  The GN source-list generator and graph validator import
this registry so those paths cannot drift independently.
"""

from __future__ import annotations

from dataclasses import dataclass
import os


@dataclass(frozen=True)
class Crate:
    """One shipping crate and the GN source-list stem retained for its target."""

    crate_id: str
    path: str
    variable: str


SHIPPING_CRATES = (
    Crate("bip-types", "taffy-core/contracts/bip/rust/bip-types", "bip_types"),
    Crate(
        "asset-plane",
        "taffy-core/components/delivery/core/rust/asset-plane",
        "asset_plane",
    ),
    Crate(
        "core-api-types",
        "taffy-core/contracts/core-api/rust/core-api-types",
        "core_api_types",
    ),
    Crate(
        "core-service-types",
        "taffy-core/contracts/core-service/rust/core-service-types",
        "core_service_types",
    ),
    Crate(
        "tool-runtime-types",
        "taffy-core/contracts/tool-runtime/rust/tool-runtime-types",
        "tool_runtime_types",
    ),
    Crate(
        "file-engine",
        "taffy-core/components/intelligence/core/rust/file-engine",
        "file_engine",
    ),
    Crate(
        "table-engine",
        "taffy-core/components/intelligence/core/rust/table-engine",
        "table_engine",
    ),
    Crate(
        "task-engine",
        "taffy-core/components/intelligence/core/rust/task-engine",
        "task_engine",
    ),
    Crate(
        "loop-kernel",
        "taffy-core/components/intelligence/core/rust/loop-kernel",
        "loop_kernel",
    ),
    Crate(
        "policy-engine",
        "taffy-core/components/security/core/rust/policy-engine",
        "policy_engine",
    ),
    Crate(
        "audit-engine",
        "taffy-core/components/security/core/rust/audit-engine",
        "audit_engine",
    ),
    Crate(
        "model-router",
        "taffy-core/components/intelligence/core/rust/model-router",
        "model_router",
    ),
    Crate(
        "procedure-engine",
        "taffy-core/components/intelligence/core/rust/procedure-engine",
        "procedure_engine",
    ),
    Crate(
        "storage-domain",
        "taffy-core/components/storage/core/rust/storage-domain",
        "storage",
    ),
    Crate(
        "tool-entrypoints",
        "taffy-core/components/tools/core/rust/tool-entrypoints",
        "tool_entrypoints",
    ),
    Crate(
        "core-runtime",
        "taffy-core/components/intelligence/core/rust/core-runtime",
        "core_runtime",
    ),
)

# Workspace crates that exist only for host tests (decision 0072). A dev crate
# is a legal workspace member with no GN target: nothing generates sources for
# it and no shipping crate may depend on it outside `[dev-dependencies]`. It is
# still named here so the three-way coverage check below stays total — a crate
# in the workspace that neither register names remains a finding.
DEV_CRATE_PATHS = (
    "taffy-core/components/intelligence/core/rust/kernel-test-support",
)

REMOVED_CRATES = {
    "agent-core": "superseded by task-engine plus core-runtime",
    "harness-ffi": "the preview-only JNI runtime is not part of the product graph",
}

_ROOT_MARKERS = ("chromium/REVISION", "TOOLCHAIN.md", "Cargo.toml")
_LEVELS_TO_REPO_ROOT = 4


class ContractError(Exception):
    """The checked tree is not a TaffyGo repository."""


def default_repo_root() -> str:
    root = os.path.dirname(os.path.realpath(__file__))
    for _ in range(_LEVELS_TO_REPO_ROOT):
        root = os.path.dirname(root)
    return root


def resolve_repo_root(explicit: str | None) -> str:
    root = os.path.abspath(explicit) if explicit else default_repo_root()
    missing = [marker for marker in _ROOT_MARKERS if not os.path.exists(os.path.join(root, marker))]
    if missing:
        raise ContractError(
            f"{root} is not a TaffyGo repository root (missing {', '.join(missing)})"
        )
    return root


def service_core_dir(repo_root: str) -> str:
    return os.path.join(repo_root, "taffy-core", "services", "core")


def source_dir(repo_root: str, crate: Crate) -> str:
    return os.path.join(repo_root, crate.path)


def manifest_path(repo_root: str, crate: Crate) -> str:
    return os.path.join(source_dir(repo_root, crate), "Cargo.toml")


def gn_source_prefix(repo_root: str, crate: Crate) -> str:
    del repo_root
    relative = crate.path.removeprefix("taffy-core/")
    return f"//taffy/{relative}"


def package_name(repo_root: str, crate: Crate) -> str:
    with open(manifest_path(repo_root, crate), encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if stripped.startswith("name") and "=" in stripped:
                return stripped.split("=", 1)[1].strip().strip('"')
    raise ContractError(f"{crate.path}/Cargo.toml declares no package name")


def gn_target_name(repo_root: str, crate: Crate) -> str:
    return package_name(repo_root, crate).replace("-", "_")


def build_file_path(repo_root: str, crate: Crate) -> str:
    return os.path.join(source_dir(repo_root, crate), "BUILD.gn")


def gn_label(repo_root: str, crate: Crate) -> str:
    relative = crate.path.removeprefix("taffy-core/")
    return f"//taffy/{relative}:{gn_target_name(repo_root, crate)}"


def workspace_members(repo_root: str) -> set[str]:
    """The `[workspace] members` paths in the root ``Cargo.toml``.

    Read line by line rather than parsed: the list is a flat array of quoted
    repository-relative paths, and a TOML library is a dependency this
    repository does not take for one array.
    """
    members: set[str] = set()
    section = None
    collecting = False
    with open(os.path.join(repo_root, "Cargo.toml"), encoding="utf-8") as handle:
        for line in handle:
            stripped = line.split("#", 1)[0].strip()
            if not collecting:
                if stripped.startswith("["):
                    section = stripped.strip("[]")
                    continue
                if section != "workspace" or not stripped.startswith("members"):
                    continue
                collecting = True
                stripped = stripped.split("=", 1)[1].strip()
            for token in stripped.strip("[],").split(","):
                entry = token.strip().strip('"')
                if entry:
                    members.add(entry)
            if "]" in stripped:
                collecting = False
    return members


def manifest_directories(repo_root: str) -> set[str]:
    """Every directory under ``taffy-core`` that holds a Cargo manifest.

    ``target`` and ``node_modules`` are pruned because both are derived trees
    that can hold manifests belonging to somebody else. ``third_party`` is
    pruned so a vendored engine (decision 0086) is not treated as a workspace
    crate: its Cargo.toml files are provenance, not TaffyGo product crates.
    """
    found: set[str] = set()
    for current, directories, files in os.walk(os.path.join(repo_root, "taffy-core")):
        directories[:] = [
            name
            for name in directories
            if name not in ("target", "node_modules", "third_party")
        ]
        if "Cargo.toml" in files:
            found.add(os.path.relpath(current, repo_root).replace(os.sep, "/"))
    return found


def verify(repo_root: str) -> list[str]:
    """Findings from the registry, read in every direction rather than one.

    The forward direction — every registered crate has a manifest — is what all
    the consumers of `SHIPPING_CRATES` already assume, and it is not enough on
    its own. A crate added to the workspace and to disk but never to this tuple
    compiles under `cargo`, is absent from the GN graph, and passes every
    forward loop in the repository, because a loop over the registry can only
    ever confirm the registry. So the three lists that have to name the same
    crates are compared as sets: the tuple below, the root manifest's
    `[workspace] members`, and the manifests actually on disk. A crate named by
    some of them and not the others is a finding that says which side is short.
    """
    findings: list[str] = []
    for crate in SHIPPING_CRATES:
        if not os.path.isfile(manifest_path(repo_root, crate)):
            findings.append(f"missing shipping crate: {crate.path}/Cargo.toml")
    for removed, reason in REMOVED_CRATES.items():
        legacy = os.path.join(repo_root, "crates", removed)
        if os.path.exists(legacy):
            findings.append(f"legacy crate still exists: crates/{removed} ({reason})")

    sides = (
        (
            "SHIPPING_CRATES or DEV_CRATE_PATHS in crate_layout.py",
            {crate.path for crate in SHIPPING_CRATES} | set(DEV_CRATE_PATHS),
        ),
        ("[workspace] members in Cargo.toml", workspace_members(repo_root)),
        ("the Cargo.toml files under taffy-core/", manifest_directories(repo_root)),
    )
    for path in sorted(set().union(*(entries for _name, entries in sides))):
        absent = [name for name, entries in sides if path not in entries]
        if not absent:
            continue
        present = [name for name, entries in sides if path in entries]
        findings.append(
            f"{path} is named by {' and '.join(present)}, but not by "
            f"{' or '.join(absent)}"
        )
    return findings
