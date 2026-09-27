# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Execute the committed compatibility corpus rather than assert it.

What was here before was a table of case names and verdicts with no payload
behind them: nothing was decoded, so "an unknown enumeration value fails
closed" was a sentence rather than a result. Every fixture now carries the
thing it is a fixture of, and the manifest declares by which artifact its
verdict is reached. The kinds exist so that no case can quietly claim more
than was run:

``payload``      committed bytes, decoded by the generated Rust decoder and by
                 the reference reader. This is the strongest kind and it needs
                 a byte codec to exist at the seam.
``enum``         a committed document whose closed enumeration carries a value
                 outside the contract, answered by the generated `from_wire`.
``bound``        a committed document measured against the generated limit
                 constant the product compiles in.
``version``      a committed document at a declared contract version, answered
                 against the version this build speaks.
``record``       a committed document answered by the contract's own shape
                 rules — tagged-union exactness, conditional presence, enum
                 pairing, field completeness — in this generator.
``declaration``  a committed schema fragment the schema validator must refuse.
``unexecuted``   a rule this contract cannot express and no artifact here can
                 run. It carries a written reason, it is counted separately,
                 and it is never reported as covered.

Only ``payload``, ``enum`` and ``bound`` reach generated code, and only its
Rust projection: running the generated Kotlin decoder needs a Kotlin compiler
and a JVM, which a stdlib-only Python generator may neither assume nor install.
The Kotlin and TypeScript projections are therefore not proven here.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import fixtures
import payload_codec
import rust_decoder_harness
from contract_schema import CONTRACT_ROOT, ContractError
from contract_validation import validate_schema
from fixture_documents import load_document, load_hex

KINDS = ("payload", "enum", "bound", "version", "record", "declaration", "unexecuted")
VERDICTS = (
    "ACCEPT",
    "REJECT_CLOSED_ENUM",
    "REJECT_MALFORMED",
    "REJECT_OVERSIZED",
    "REJECT_VERSION",
)
NARRATIVE = ("rule", "deviation", "note")


def manifest_path(contract: str) -> Path:
    return CONTRACT_ROOT / contract / "compat" / "manifest.json"


def load_manifest(contract: str) -> dict[str, Any]:
    path = manifest_path(contract)
    value = load_document(path)
    entries = value.get("fixtures")
    if not isinstance(entries, list) or not entries:
        raise ContractError(f"{path}: the corpus must declare a non-empty fixtures list")
    seen: set[str] = set()
    for entry in entries:
        case = entry.get("case")
        if not isinstance(case, str) or case in seen:
            raise ContractError(f"{path}: invalid or duplicate case {case!r}")
        seen.add(case)
        if entry.get("kind") not in KINDS:
            raise ContractError(f"{path}: {case} has no known kind")
        if entry.get("verdict") not in VERDICTS:
            raise ContractError(f"{path}: {case} has no known verdict")
        for field in NARRATIVE:
            if not isinstance(entry.get(field), str) or not entry[field].strip():
                raise ContractError(f"{path}: {case} needs a written {field}")
        if entry["kind"] == "unexecuted":
            if not isinstance(entry.get("reason"), str) or not entry["reason"].strip():
                raise ContractError(
                    f"{path}: {case} runs nothing, so it must say in writing what "
                    "would have to exist before it could"
                )
        elif not isinstance(entry.get("file"), str):
            raise ContractError(f"{path}: {case} names no fixture file")
    _check_every_file_is_used(contract, entries)
    return value


def _check_every_file_is_used(contract: str, entries: list[dict[str, Any]]) -> None:
    """Both directions: no unnamed file on disk, no named file missing."""
    directory = manifest_path(contract).parent
    named = {entry["file"] for entry in entries if entry["kind"] != "unexecuted"}
    on_disk = {
        item.name for item in directory.iterdir() if item.name != "manifest.json"
    }
    for missing in sorted(named - on_disk):
        raise ContractError(f"{contract}: the corpus names {missing}, which is not on disk")
    for stray in sorted(on_disk - named):
        raise ContractError(f"{contract}: {stray} is in the corpus and no fixture names it")


def _walk(document: Any, path: str) -> Any:
    value = document
    for step in path.split("."):
        name, _, index = step.partition("[")
        value = value[name]
        if index:
            value = value[int(index.rstrip("]"))]
    return value


def _measure(value: Any) -> int:
    if isinstance(value, str):
        return len(value.encode("utf-8"))
    if isinstance(value, list):
        return len(value)
    if isinstance(value, int):
        return value
    raise ContractError("a bound fixture must measure a string, a list, or a number")


def _version_verdict(schema: dict[str, Any], declared: str) -> str:
    """What this build does with a message that declares another version.

    A different major version changes required meaning, so the message is
    refused on the version alone and its body is never consulted. A higher
    minor version is additive by construction, so the fields this build does
    not know are ignored rather than treated as an error — which is the rule
    that lets the contract grow at all.
    """
    major, _, minor = declared.partition(".")
    try:
        pair = (int(major), int(minor))
    except ValueError as error:
        raise ContractError(f"{declared!r} is not a major.minor contract version") from error
    if pair[0] != schema["version"]["major"]:
        return "REJECT_VERSION"
    return "ACCEPT"


def _assert_next_minor(
    contract: str, case: str, schema: dict[str, Any], declared: str
) -> None:
    """That a fixture written to be *one minor ahead* still is one.

    [`_version_verdict`] compares only the major, on purpose — that is the
    rule it implements. The cost is that a fixture whose whole subject is "a
    message from the next minor version" keeps answering ACCEPT after the
    schema passes it, because a past minor and a future minor are the same
    answer to a comparison that reads the major alone. It has silently become
    a message from a *previous* minor and the rule it exists for is no longer
    under test anywhere.

    This is not hypothetical and it is not rare. Core API's fixture was
    written against a 3.1 schema, declared 3.2, and was still 3.2 when the
    contract reached 3.6 — four minors on the wrong side of the boundary it
    exists to check, verifying green the whole way. BIP's reached eleven,
    declaring 0.2 against a 0.13 protocol, and was closed by exactly this
    check in its own generator. The four corpora that share this module were
    left to a paragraph in `AGENTS.md` telling a reader to count them by hand.
    """
    major, _, minor = declared.partition(".")
    want = (schema["version"]["major"], schema["version"]["minor"] + 1)
    if (int(major), int(minor)) != want:
        raise ContractError(
            f"{contract}: {case} exists to prove a message from the next minor "
            f"version is accepted, so it must declare {want[0]}.{want[1]}, not "
            f"{declared}. Raise it in the same change that raises the schema — "
            f"a fixture from a past minor proves nothing and still passes."
        )


def _known_fields_only(schema: dict[str, Any], type_name: str, message: Any) -> Any:
    structs = {item["name"]: item for item in schema["structs"]}
    known = {field["name"] for field in structs[type_name]["fields"]}
    return {name: value for name, value in message.items() if name in known}


def _declaration_fragment(schema: dict[str, Any], fixture: dict[str, Any]) -> dict[str, Any]:
    return {
        "contract": schema["contract"],
        "namespace": schema["namespace"],
        "version": schema["version"],
        "projections": schema["projections"],
        "limits": {"MAX_COMPAT_FIXTURE_BYTES": 1},
        "forbidden_field_substrings": schema.get("forbidden_field_substrings", []),
        "enums": fixture.get("enums", []),
        "structs": fixture.get("structs", []),
        "interfaces": fixture.get("interfaces", []),
    }


def _document_case(
    contract: str, schema: dict[str, Any], entry: dict[str, Any], document: Any
) -> None:
    """Check the half of a document fixture this generator can answer itself."""
    case = entry["case"]
    kind = entry["kind"]
    if kind == "declaration":
        try:
            validate_schema(contract, _declaration_fragment(schema, document))
        except ContractError as refusal:
            # A fragment refused for a reason the corpus did not intend is a
            # fixture that passes while proving nothing, so the row may name
            # the words the refusal has to contain. Once one rule is spelled
            # out this way, a later rule that happens to fire first can no
            # longer stand in for it.
            expected = entry.get("refusal")
            if expected is not None and expected not in str(refusal):
                raise ContractError(
                    f"{contract}: {case} is refused, but for {refusal!s} rather "
                    f"than the rule it exists for ({expected!r})"
                ) from refusal
            return
        raise ContractError(f"{contract}: {case} declares a record the validator accepts")
    if kind == "version":
        declared = document.get("contract_version")
        reached = _version_verdict(schema, declared)
        if reached != entry["verdict"]:
            raise ContractError(
                f"{contract}: {case} declares version {declared}, which this build "
                f"answers with {reached}, not {entry['verdict']}"
            )
        if entry.get("declares_next_minor"):
            _assert_next_minor(contract, case, schema, declared)
        message = _known_fields_only(schema, entry["type"], document["message"])
        body = fixtures.document_verdict(schema, entry["type"], message)
        if body != "ACCEPT":
            raise ContractError(
                f"{contract}: {case} must be refused on its version alone, but the "
                f"fields this build knows are themselves {body}"
            )
        return
    if "type" not in entry:
        # A bound fixture may be a measurement rather than a record, for a limit
        # whose real fixture would be a quarter of a megabyte of hexadecimal.
        if kind != "bound":
            raise ContractError(f"{contract}: {case} names no record type")
        return
    expected = "ACCEPT" if kind == "bound" else entry["verdict"]
    reached = fixtures.document_verdict(schema, entry["type"], document)
    if reached != expected:
        raise ContractError(
            f"{contract}: {case} reaches {reached} under this contract's own rules, "
            f"and the corpus says {expected}"
        )


def _rust_jobs(
    contract: str, entries: list[dict[str, Any]], documents: dict[str, Any]
) -> list[tuple[str, tuple[str, str, str], str]]:
    """One `(case, job, expected answer)` for every fixture Rust can answer."""
    jobs: list[tuple[str, tuple[str, str, str], str]] = []
    for entry in entries:
        case = entry["case"]
        path = manifest_path(contract).parent / entry.get("file", "")
        if entry["kind"] == "payload":
            # An accepted payload has no error to name, and the harness says so
            # in the same word the reference reader does.
            expected = entry["decoder_error"] or "ACCEPT"
            jobs.append((case, ("payload", entry["codec"], str(path)), expected))
        elif entry["kind"] == "enum":
            wire = _walk(documents[case], entry["path"])
            jobs.append((case, ("enum", entry["enum"], str(wire)), "UNSUPPORTED"))
        elif entry["kind"] == "bound":
            measured = _measure(_walk(documents[case], entry["path"]))
            jobs.append((case, ("limit", entry["limit"], str(measured)), "OVER"))
    return jobs


def verify(contract: str, schema: dict[str, Any], models: dict[str, Any]) -> str:
    """Run every executable fixture, and report exactly what was executed."""
    manifest = load_manifest(contract)
    entries = manifest["fixtures"]
    documents: dict[str, Any] = {}
    directory = manifest_path(contract).parent
    for entry in entries:
        case = entry["case"]
        if entry["kind"] == "unexecuted":
            continue
        if entry["kind"] == "payload":
            data = load_hex(directory / entry["file"])
            model = models.get(entry.get("codec"))
            if model is None:
                raise ContractError(f"{contract}: {case} names no declared codec")
            if (entry["decoder_error"] is None) != (entry["verdict"] == "ACCEPT"):
                raise ContractError(
                    f"{contract}: {case} must name the generated decoder error it "
                    "expects, and only an accepted payload may name none"
                )
            verdict, error = payload_codec.verdict(model, data)
            if (verdict, error) != (entry["verdict"], entry["decoder_error"]):
                raise ContractError(
                    f"{contract}: {case} decodes to {verdict}/{error}, and the corpus "
                    f"says {entry['verdict']}/{entry['decoder_error']}"
                )
            continue
        documents[case] = load_document(directory / entry["file"])
        _document_case(contract, schema, entry, documents[case])

    jobs = _rust_jobs(contract, entries, documents)
    answers = rust_decoder_harness.run(
        rust_decoder_harness.generated_sources(contract, _rust_sources(contract, schema)),
        _RUST_CODECS.get(contract, {}),
        rust_decoder_harness.enum_names(schema),
        rust_decoder_harness.limit_names(schema),
        [job for _case, job, _expected in jobs],
    )
    if answers is None:
        proven = "the generated Rust decoder was NOT run on this host (rustc is absent)"
    else:
        for (case, _job, expected), answer in zip(jobs, answers):
            if answer != expected:
                raise ContractError(
                    f"{contract}: the generated Rust decoder answers {case} with "
                    f"{answer}, and the corpus says {expected}"
                )
        proven = f"{len(jobs)} run against the generated Rust decoder"
    counts = {kind: sum(1 for entry in entries if entry["kind"] == kind) for kind in KINDS}
    listed = ", ".join(f"{counts[kind]} {kind}" for kind in KINDS if counts[kind])
    # Naming the projections that were not exercised, from what the contract
    # says it emits rather than from a sentence written once. A report that
    # lists a language this contract does not project is as misleading as one
    # that leaves out a language it does.
    names = {"kotlin": "Kotlin", "typescript": "TypeScript"}
    unproven = [
        names[name] for name in names if name in schema["projections"]
    ]
    unrun = (
        "; the generated "
        + " and ".join(unproven)
        + (" decoders were not run" if len(unproven) > 1 else " decoder was not run")
    ) if unproven else ""
    return f"{contract} compat: {len(entries)} fixtures ({listed}); {proven}{unrun}"


def _rust_sources(contract: str, schema: dict[str, Any]) -> list[str]:
    """The generated Rust files this corpus compiles, discovered from disk.

    Discovered rather than tabulated, and in that direction on purpose. A
    hand-kept list of filenames is a second description of what the generator
    emits; the two disagree the first time a contract emits one file rather
    than two, and the corpus then runs against fewer decoders than its own
    report claims. Reading the directory cannot make that mistake, and it is
    the same rule the fixture corpora are held to: a file on disk that no
    register names is the case a register cannot see.

    The contract's own module comes first because the codec module beside it
    names its types.
    """
    if "rust" not in schema["projections"]:
        return []
    root = CONTRACT_ROOT / contract / "generated" / "rust"
    stem = f"{contract.replace('-', '_')}.rs"
    found = sorted(path.name for path in root.glob("*.rs"))
    if stem not in found:
        raise ContractError(f"{contract}: generated/rust/{stem} is not on disk")
    return [stem] + [name for name in found if name != stem]


_RUST_CODECS = {
    "core-api": {"core_status_payload": "decode_core_status_payload"},
    "core-service": {"transaction_batch": "decode_transaction_batch"},
    "tool-runtime": {},
}
