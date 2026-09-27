#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate the BIP bindings from the normative JSON Schema contract.

The schemas under ``taffy-core/contracts/bip/schema`` are the single source of truth for
the Browser Intelligence Protocol wire format. This script reads them and
writes the Rust view of the same contract, and compares every other surface
against it, so five consumers cannot drift into five slightly different ideas
of what a page snapshot is.

Rust is the only complete generated language view. The generator also emits
the one C++ protocol-version constant used by the browser/renderer transport,
so a schema bump cannot leave shipping messages advertising the old version.
BIP terminates at the browser-side page intelligence adapter, and Android
consumes the separate, profile-scoped Core API rather than this contract, so
there is no Kotlin view to keep current. Generating one anyway would have
published raw BIP enumerations to a layer that is not trusted to hold them.

    python3 taffy-core/contracts/bip/codegen/generate.py --write        regenerate
    python3 taffy-core/contracts/bip/codegen/generate.py --check        fail if stale
    python3 taffy-core/contracts/bip/codegen/generate.py --verify       check the fixtures
    python3 taffy-core/contracts/bip/codegen/generate.py --mojom        check the Mojo and C++ views
    python3 taffy-core/contracts/bip/codegen/generate.py --self-test    check the generator

Host Python 3 only: no third-party package, no network, no build step. Output is
deterministic, so ``--check`` in continuous integration is a real gate: schema
definitions are emitted in sorted order, enumeration members keep the order the
schema declares (the action result taxonomy is normative in its order), and
nothing carries a timestamp, a host name, or a random value.

What the generator understands, and nothing more:

The Mojo definitions in ``taffy-core/contracts/bip/mojom`` are not
generated — they carry trust-boundary reasoning a generator cannot express —
so ``--mojom`` compares them instead, and then compares the hand-written C++
enumerations in the overlay against the Mojo file. Schema to Rust, and schema
to Mojo to C++, is the whole chain, and it is checkable here because every link
is text. That is the check work package WP-M2-01 is verified by, and it needs
no Chromium checkout.

* a definition with ``enum``          -> a closed Rust enumeration
* a definition with ``properties``    -> a Rust struct
* any other scalar definition         -> a newtype wrapper, so a ``TabId`` can
  never be passed where a ``FrameId`` belongs

A definition it cannot classify is an error, never a silently skipped type.

How this generator is laid out
------------------------------

One module per responsibility, so a change to Rust spelling cannot reach the
fixture checks by accident:

    layout.py         where the contract lives, and the two shared constants
    naming.py         wire name to identifier, once, for every surface
    contract.py       the schema loaded and indexed; classification lives here
    render_rust.py    the Rust view
    render_cxx.py     the generated C++ protocol-version constant
    outputs.py        the output plan, --write and --check over it
    fixtures.py       --verify: golden and compatibility fixtures
    mojom.py          --mojom: the hand-written Mojo projection, compared
    cxx.py            --mojom: the C++ enumerations that mirror that projection
    selftest.py       --self-test: the generator's own invariants

This file is the command line and nothing else.
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Local modules, resolved by the line above.
import cxx  # noqa: E402
import fixtures  # noqa: E402
import mojom  # noqa: E402
import outputs  # noqa: E402
import selftest  # noqa: E402
from contract import Contract  # noqa: E402
from layout import SCHEMA_DIR  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true", help="regenerate the bindings")
    group.add_argument("--check", action="store_true", help="fail if the bindings are stale")
    group.add_argument("--verify", action="store_true", help="check golden and compat fixtures")
    group.add_argument(
        "--mojom",
        action="store_true",
        help="check the Mojo projection against the schema, and its C++ mirrors against it",
    )
    group.add_argument("--self-test", action="store_true", help="check the generator itself")
    args = parser.parse_args(argv)

    if args.self_test:
        return selftest.run()

    contract = Contract(SCHEMA_DIR)
    findings = contract.audit()
    for finding in findings:
        print(f"schema: {finding}")
    if findings:
        print()
        print(f"{len(findings)} schema finding(s); nothing was generated")
        return 1

    if args.verify:
        return fixtures.verify(contract)

    if args.mojom:
        # Order matters: comparing C++ against a Mojo file that already
        # disagrees with the schema would report the same drift twice and name
        # the wrong file as the defect.
        status = mojom.report(contract)
        if status:
            return status
        declarations, _ = mojom.parse_projection()
        return cxx.report(declarations)

    plan = outputs.build(contract, outputs.load_version())
    return outputs.write(plan) if args.write else outputs.check(plan)


if __name__ == "__main__":
    sys.exit(main())
