#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Compile the generated TypeScript, under settings stricter than any consumer here.

Every contract emits TypeScript, and until this existed none of it had been
through a compiler. `--check` proves the bytes match the generator; it says
nothing about whether the generator emits code that builds.

The settings below were once the union of the two TypeScript projects in this
repository. One of them, the managed-route Worker, has left (decision 0200),
and the three options it alone contributed — `noImplicitOverride`,
`noFallthroughCasesInSwitch`, `noPropertyAccessFromIndexSignature` — are kept
anyway, so this is now deliberately stricter than `website/`, the only
consumer left.

Keeping them is the point. What this compiles is *generated* code, and the
consumer that has to build it is the one nobody has written yet: a WebUI, a
desktop surface, an extension. Relaxing a setting because the project that
motivated it was deleted would trade a real property of the generators —
their output builds under strict settings — for a fact about which directories
happen to exist this week. The generators pass today with all three on, so
there is nothing to pay for keeping them.

Two of those settings do real work rather than tidying:

* `verbatimModuleSyntax` refuses a value import of a type that is erased at run
  time, which is why the codec imports enums and interfaces separately.
* `noUncheckedIndexedAccess` types every index expression as possibly
  undefined, which is what forces a decoder's bounds check to be a branch
  rather than a claim.

`lib` is `ES2022` alone, with no DOM. The only platform globals the generated
code may use are `TextEncoder` and `TextDecoder`, declared by
`typescript_platform.d.ts` beside this file. A generator that ever reaches for
`document`, `fetch`, `Buffer` or a timer fails here instead of compiling
against a runtime it may not be in — the declaration file is the neutrality
statement, and the compiler enforces it.

`tsc` is not required. The contracts lane's readiness predicate promises the
generator runs on any host with `python3`, and that promise is kept: when no
compiler is found this reports, in as many words, that the generated
TypeScript was not compiled on this host, and exits 0. It never reports a pass
for something it did not run.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
PLATFORM_DECLARATIONS = Path(__file__).resolve().parent / "typescript_platform.d.ts"

#: Where a pinned compiler lives. Workspace-local, so this installs nothing and
#: resolves no registry.
VENDORED_COMPILERS = (
    Path("website") / "node_modules" / ".bin" / "tsc",
)

#: website/tsconfig.json minus the parts about its own framework, plus the three
#: strict options the retired Worker contributed. See the module docstring for
#: why those three stay.
COMPILER_OPTIONS = {
    "target": "ES2022",
    "lib": ["ES2022"],
    "module": "esnext",
    "moduleResolution": "bundler",
    "types": [],
    "strict": True,
    "noUncheckedIndexedAccess": True,
    "noImplicitOverride": True,
    "noFallthroughCasesInSwitch": True,
    "noPropertyAccessFromIndexSignature": True,
    "isolatedModules": True,
    "verbatimModuleSyntax": True,
    "skipLibCheck": True,
    "noEmit": True,
}


def generated_sources() -> list[Path]:
    return sorted(ROOT.glob("*/generated/typescript/*.ts"))


def find_compiler() -> Path | None:
    for relative in VENDORED_COMPILERS:
        candidate = REPO / relative
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    found = shutil.which("tsc")
    return Path(found) if found else None


def compile_sources(compiler: Path, sources: list[Path]) -> tuple[int, str]:
    """Type-check `sources` and return (exit code, diagnostics)."""
    with tempfile.TemporaryDirectory() as directory:
        config = Path(directory) / "tsconfig.json"
        config.write_text(
            json.dumps(
                {
                    "compilerOptions": COMPILER_OPTIONS,
                    "files": [str(PLATFORM_DECLARATIONS)] + [str(path) for path in sources],
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        answered = subprocess.run(
            [str(compiler), "-p", str(config)],
            capture_output=True,
            text=True,
            check=False,
        )
    return answered.returncode, (answered.stdout + answered.stderr).strip()


def _self_test(compiler: Path) -> None:
    """Prove the check can fail, by handing it code that must not compile.

    A gate that has only ever been run against a passing tree is a gate nobody
    has seen refuse anything.
    """
    cases = {
        "an unchecked index": "export function first(bytes: Uint8Array): number {\n"
        "  return bytes[0];\n"
        "}\n",
        "a value import of a type": 'import { CoreStatus } from "./core_api";\n'
        "export const held: CoreStatus | null = null;\n",
        "a platform global that is not declared": "export const where = document.title;\n",
    }
    with tempfile.TemporaryDirectory() as directory:
        for description, body in cases.items():
            source = Path(directory) / "case.ts"
            source.write_text(body, encoding="utf-8")
            code, _ = compile_sources(compiler, [source])
            if code == 0:
                raise SystemExit(
                    f"typescript typecheck self-test: {description} compiled, "
                    "so this check would not have caught it"
                )
    print("typescript typecheck self-test passed (3 refusals)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    compiler = find_compiler()
    if compiler is None:
        print(
            "typescript typecheck: not run — no tsc on this host. Install the "
            "JS workspace (pnpm --dir website install) to compile the "
            "generated TypeScript; this check reports nothing it did not run."
        )
        return 0

    if args.self_test:
        _self_test(compiler)
        return 0

    sources = generated_sources()
    if not sources:
        print("typescript typecheck: no generated TypeScript found under contracts/")
        return 1
    code, diagnostics = compile_sources(compiler, sources)
    if code != 0:
        print(diagnostics or "typescript typecheck: tsc failed without diagnostics")
        return 1
    total = sum(len(path.read_text(encoding="utf-8").splitlines()) for path in sources)
    print(
        f"generated TypeScript: {len(sources)} file(s), {total} lines compile "
        f"under settings stricter than any consumer in this repository"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
