#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Emit cargo_crate() targets for the vendored adblock-rust dependency graph.

Walks ``vendor/*/Cargo.toml``, resolves features from the adblock crate, and
writes ``crates.gni``. Chromium's cargo_crate template compiles each rlib;
this script does not invent a second graph.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / "vendor"
OUT = ROOT / "BUILD.gn"
ADBLOCK_PACKAGE = "adblock"
ABI_FOOTER = '''
rust_static_library("taffy_adblock_c") {
  crate_name = "taffy_adblock_c"
  crate_root = "c_abi/src/lib.rs"
  sources = [ "c_abi/src/lib.rs" ]
  allow_unsafe = true
  edition = "2024"
  no_chromium_prelude = true
  no_clippy = true
  deps = [
    ":adblock",
    ":serde_json",
  ]
  configs -= [ "//build/config/compiler:chromium_code" ]
  configs += [ "//build/config/compiler:no_chromium_code" ]
  visibility = [ ":c_abi" ]
}

source_set("c_abi") {
  public = [ "c_abi/include/taffy_adblock.h" ]
  public_deps = [ ":taffy_adblock_c" ]
  visibility = [ "//taffy/components/filtering/core:*" ]
}
'''
SKIP_BUILD_SCRIPTS = {
    "flatbuffers",
    "icu_normalizer_data",
    "icu_properties_data",
}
BUILD_SCRIPT_OUTPUTS = {
    "serde": ["private.rs"],
    "serde_core": ["private.rs"],
}
SKIP_SOURCE_DIRS = {"tests", "benches", "examples", "tests_impl", "benches_impl"}


def epoch(version: str) -> str:
    core = version.split("+", 1)[0].split("-", 1)[0]
    parts = [int(p) for p in core.split(".")]
    parts += [0] * (3 - len(parts))
    major, minor, patch = parts[0], parts[1], parts[2]
    if major > 0:
        return str(major)
    if minor > 0:
        return f"0.{minor}"
    return f"0.0.{patch}"


def rust_crate_name(package: str) -> str:
    return package.replace("-", "_")


def parse_spec(spec: object) -> dict:
    if isinstance(spec, str):
        return {
            "version": spec,
            "optional": False,
            "default_features": True,
            "features": [],
            "package": None,
        }
    if not isinstance(spec, dict):
        return {
            "version": "",
            "optional": False,
            "default_features": True,
            "features": [],
            "package": None,
        }
    return {
        "version": str(spec.get("version", "")),
        "optional": bool(spec.get("optional", False)),
        "default_features": bool(spec.get("default-features", True)),
        "features": list(spec.get("features", [])),
        "package": spec.get("package"),
    }


def load_crates() -> dict[str, dict]:
    crates: dict[str, dict] = {}
    for manifest in sorted(VENDOR.glob("*/Cargo.toml")):
        data = tomllib.loads(manifest.read_text(encoding="utf-8"))
        package = data.get("package") or {}
        name = package.get("name")
        if not name:
            continue
        lib = data.get("lib") or {}
        key = manifest.parent.name
        crates[key] = {
            "name": name,
            "version": package.get("version", "0.0.0"),
            # Cargo's default when the manifest omits `edition` is 2015.
            "edition": str(package.get("edition", "2015")),
            "authors": package.get("authors", ""),
            "description": (package.get("description") or "").split("\n", 1)[0],
            "repository": package.get("repository", ""),
            "dir": manifest.parent,
            "lib_path": lib.get("path", "src/lib.rs"),
            "proc_macro": bool(lib.get("proc-macro", False)),
            "features": data.get("features") or {},
            "dependencies": {
                dep: parse_spec(spec)
                for dep, spec in (data.get("dependencies") or {}).items()
            },
            "build_dependencies": {
                dep: parse_spec(spec)
                for dep, spec in (data.get("build-dependencies") or {}).items()
            },
            "has_build": (manifest.parent / "build.rs").is_file()
            and name not in SKIP_BUILD_SCRIPTS,
        }
    return crates


def major_of(version: str) -> int:
    core = version.split("+", 1)[0].split("-", 1)[0]
    return int(core.split(".")[0])


def pick_crate(crates: dict[str, dict], package: str, version_req: str) -> str | None:
    matches = [key for key, crate in crates.items() if crate["name"] == package]
    if not matches:
        return None
    if len(matches) == 1:
        return matches[0]
    want_major = None
    if version_req:
        digits = re.match(r"[^0-9]*(\d+)", version_req)
        if digits:
            want_major = int(digits.group(1))
    if want_major is not None:
        for key in matches:
            if major_of(crates[key]["version"]) == want_major:
                return key
    return matches[0]


def rustc_features(features: set[str]) -> list[str]:
    # rustc --cfg=feature="a/b" is not a valid cfg. Cargo uses `dep/feature`
    # only as an activation syntax, never as a rustc feature name.
    # Do not emit cargo_crate.no_std: that flag drops //build/rust/std, which
    # is how Chromium's rustc finds `core` for aarch64-linux-android.
    return sorted(
        name
        for name in features
        if "/" not in name and not name.startswith("dep:")
    )


def extra_build_script_inputs(crate: dict) -> list[str]:
    out: list[str] = []
    build_dir = crate["dir"] / "build"
    if not build_dir.is_dir():
        return out
    for path in sorted(build_dir.rglob("*")):
        if not path.is_file() or path.name == "build.rs":
            continue
        rel = path.relative_to(crate["dir"]).as_posix()
        out.append(f"vendor/{crate['dir'].name}/{rel}")
    return out


def expand_features(crate: dict, enabled: set[str]) -> set[str]:
    features = crate["features"]
    out = set(enabled)
    changed = True
    while changed:
        changed = False
        for name in list(out):
            for item in features.get(name, []):
                if item.startswith("dep:"):
                    continue
                if "/" in item:
                    continue
                if item not in out:
                    out.add(item)
                    changed = True
    return out


def resolve_graph(crates: dict[str, dict]) -> dict[str, set[str]]:
    enabled: dict[str, set[str]] = {}
    queue: list[str] = []

    def activate(name: str, extras: set[str], use_default: bool) -> None:
        crate = crates[name]
        wanted = set(extras)
        if use_default:
            wanted.update(crate["features"].get("default", []))
        wanted = expand_features(crate, wanted)
        previous = enabled.get(name)
        if previous is None:
            enabled[name] = wanted
            queue.append(name)
            return
        if not wanted <= previous:
            enabled[name] = previous | wanted
            queue.append(name)

    root = next(key for key, crate in crates.items() if crate["name"] == ADBLOCK_PACKAGE)
    activate(root, set(), True)
    while queue:
        name = queue.pop()
        crate = crates[name]
        feats = enabled[name]
        needed_deps: dict[str, tuple[set[str], bool]] = {}
        for dep, spec in crate["dependencies"].items():
            activate_dep = not spec["optional"]
            dep_features = set(spec["features"])
            if dep in feats:
                activate_dep = True
            for feature in feats:
                for item in crate["features"].get(feature, []):
                    if item == f"dep:{dep}":
                        activate_dep = True
                    elif item.startswith(f"{dep}/") or item.startswith(f"{dep}?/"):
                        activate_dep = True
                        dep_features.add(item.split("/", 1)[1])
            if not activate_dep:
                continue
            package = spec["package"] or dep
            key = pick_crate(crates, package, spec["version"])
            if key is None:
                continue
            current = needed_deps.get(key)
            if current is None:
                needed_deps[key] = (dep_features, spec["default_features"])
            else:
                needed_deps[key] = (
                    current[0] | dep_features,
                    current[1] or spec["default_features"],
                )
        for key, (dep_features, use_default) in needed_deps.items():
            activate(key, dep_features, use_default)
        if crate["has_build"]:
            for dep, spec in crate["build_dependencies"].items():
                if spec["optional"]:
                    continue
                package = spec["package"] or dep
                key = pick_crate(crates, package, spec["version"])
                if key is not None:
                    activate(key, set(spec["features"]), spec["default_features"])
    return enabled


def gn_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def gn_string_list(values: list[str], indent: int) -> str:
    pad = " " * indent
    if not values:
        return "[]"
    inner = ",\n".join(f'{pad}  "{gn_escape(value)}"' for value in values)
    return f"[\n{inner},\n{pad}]"


def crate_files(crate: dict) -> tuple[list[str], list[str]]:
    sources: list[str] = []
    inputs: list[str] = []
    root = crate["dir"]
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        parts = rel.split("/")
        if parts[0] in SKIP_SOURCE_DIRS:
            continue
        if path.name == "build.rs":
            continue
        if path.suffix == ".rs":
            sources.append(f"vendor/{root.name}/{rel}")
        elif path.suffix in {".data", ".bin"} or path.parent.name == "data":
            inputs.append(f"vendor/{root.name}/{rel}")
    crate_root = f"vendor/{root.name}/{crate['lib_path']}"
    if crate_root not in sources:
        sources.insert(0, crate_root)
    return sources, inputs


def emit_target(
    name: str,
    crate: dict,
    features: set[str],
    crates: dict[str, dict],
    target_names: dict[str, str],
) -> str:
    crate_name = rust_crate_name(crate["name"])
    target = target_names[name]
    sources, inputs = crate_files(crate)
    crate_root = f"vendor/{crate['dir'].name}/{crate['lib_path']}"
    deps: list[str] = []
    seen_deps: set[str] = set()
    feats = expand_features(crate, features)
    for dep, spec in crate["dependencies"].items():
        activate_dep = not spec["optional"]
        if dep in feats:
            activate_dep = True
        for feature in feats:
            for item in crate["features"].get(feature, []):
                if (
                    item == f"dep:{dep}"
                    or item.startswith(f"{dep}/")
                    or item.startswith(f"{dep}?/")
                ):
                    activate_dep = True
        if not activate_dep:
            continue
        package = spec["package"] or dep
        key = pick_crate(crates, package, spec["version"])
        if key is None or key not in target_names:
            continue
        label = f":{target_names[key]}"
        if label not in seen_deps:
            seen_deps.add(label)
            deps.append(label)
    deps.sort()
    feature_list = rustc_features(feats)
    lines = [
        f'cargo_crate("{target}") {{',
        f'  crate_name = "{crate_name}"',
        f'  epoch = "{epoch(crate["version"])}"',
        # Chromium vendors many of these same crates at the same epoch, and
        # cargo_crate's default metadata is "<crate>-<epoch>", so both copies
        # would compile to the same disambiguator and any binary that reached
        # both would fail to link on duplicate symbols — not TaffyGo's symbols
        # but the `core::` generics each rlib instantiates for itself. That is
        # not hypothetical: //taffy/browser:browser_shared reaches this graph
        # through the filtering component and Chromium's through base/i18n, and
        # the thirteen fuzzers link both. Name this copy explicitly so the two
        # can coexist; it changes symbol names only.
        f'  rustc_metadata = "taffy-adblock-{crate_name}-{epoch(crate["version"])}"',
        f'  crate_type = "{"proc-macro" if crate["proc_macro"] else "rlib"}"',
        f'  crate_root = "{crate_root}"',
        f"  sources = {gn_string_list(sources, 2)}",
        f"  inputs = {gn_string_list(inputs, 2)}",
        "  build_native_rust_unit_tests = false",
        f'  edition = "{crate["edition"]}"',
        f'  cargo_pkg_name = "{crate["name"]}"',
        f'  cargo_pkg_version = "{crate["version"]}"',
        f'  cargo_pkg_description = "{gn_escape(crate["description"])}"',
    ]
    if crate["repository"]:
        lines.append(f'  cargo_pkg_repository = "{gn_escape(crate["repository"])}"')
    authors = crate["authors"]
    if isinstance(authors, list):
        authors = ", ".join(authors)
    if authors:
        lines.append(f'  cargo_pkg_authors = "{gn_escape(str(authors))}"')
    lines.append("  allow_unsafe = true")
    if deps:
        lines.append(f"  deps = {gn_string_list(deps, 2)}")
    if feature_list:
        lines.append(f"  features = {gn_string_list(feature_list, 2)}")
    if crate["has_build"]:
        build_rel = f"vendor/{crate['dir'].name}/build.rs"
        lines.append(f'  build_root = "{build_rel}"')
        lines.append(f"  build_sources = [ \"{build_rel}\" ]")
        script_inputs = extra_build_script_inputs(crate)
        if script_inputs:
            lines.append(f"  build_script_inputs = {gn_string_list(script_inputs, 2)}")
        outputs = BUILD_SCRIPT_OUTPUTS.get(crate["name"], [])
        if outputs:
            quoted = ", ".join(f'"{item}"' for item in outputs)
            lines.append(f"  build_script_outputs = [ {quoted} ]")
    lines.extend(
        [
            "  library_configs -= [",
            '    "//build/config/coverage:default_coverage",',
            '    "//build/config/compiler:chromium_code",',
            "  ]",
            "  executable_configs -= [",
            '    "//build/config/coverage:default_coverage",',
            '    "//build/config/compiler:chromium_code",',
            "  ]",
            "  proc_macro_configs -= [",
            '    "//build/config/coverage:default_coverage",',
            '    "//build/config/compiler:chromium_code",',
            "  ]",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def target_name_map(enabled: dict[str, set[str]], crates: dict[str, dict]) -> dict[str, str]:
    used: dict[str, list[str]] = {}
    for name in enabled:
        crate_name = rust_crate_name(crates[name]["name"])
        used.setdefault(crate_name, []).append(name)
    names: dict[str, str] = {}
    for crate_name, packages in used.items():
        if len(packages) == 1:
            names[packages[0]] = crate_name
            continue
        for package in packages:
            names[package] = f"{crate_name}_v{epoch(crates[package]['version']).replace('.', '_')}"
    return names


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args(argv)
    crates = load_crates()
    adblock_key = pick_crate(crates, ADBLOCK_PACKAGE, "0.13.3")
    if adblock_key is None:
        print("generate_crate_gn: adblock crate is missing from vendor/", file=sys.stderr)
        return 1
    enabled = resolve_graph(crates)
    names = target_name_map(enabled, crates)
    chunks = [
        "# Copyright (c) 2026 Matterward Labs Private Limited.",
        "#",
        "# This Source Code Form is subject to the terms of the Mozilla Public",
        "# License, v. 2.0. If a copy of the MPL was not distributed with this",
        "# file, You can obtain one at https://mozilla.org/MPL/2.0/.",
        "#",
        "# @generated by tools/generate_crate_gn.py. Do not edit.",
        "",
        'import("//build/rust/cargo_crate.gni")',
        'import("//build/rust/rust_static_library.gni")',
        "",
    ]
    for name in sorted(enabled, key=lambda item: names[item]):
        chunks.append(emit_target(name, crates[name], enabled[name], crates, names))
    chunks.append(ABI_FOOTER.lstrip("\n"))
    text = "\n".join(chunks)
    if args.check:
        if not OUT.is_file() or OUT.read_text(encoding="utf-8") != text:
            print(f"{OUT}: stale; run tools/generate_crate_gn.py --write", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} ({len(enabled)} crates)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
