#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The generator checking itself.

Authority boundary: properties of the generator, not of the contract. It runs
before `--check` is trusted, because a generator whose own invariants are
broken produces output that cannot be trusted either.
"""

from __future__ import annotations

import os
import tempfile

import cxx
import fixtures
import jsonschema_mini
import mojom
import render_cxx
import render_rust
from contract import Contract, version_tuple
from layout import GENERATED_MARK, GeneratorError
from naming import escape_doc, pascal_case, rust_field

def run() -> int:
    checks = 0

    def expect(condition: bool, label: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(label)

    expect(pascal_case("STALE_PAGE_EPOCH") == "StalePageEpoch", "pascal_case snake")
    expect(pascal_case("JSON_LD") == "JsonLd", "pascal_case acronym")
    expect(pascal_case("VERIFIED") == "Verified", "pascal_case single word")
    expect(rust_field("type") == "r#type", "rust keyword escape")
    expect(escape_doc("[Open (OD-031)]") == "\\[Open (OD-031)\\]", "doc bracket escape")

    documents = {
        "toy.schema.json": {
            "$id": "toy.schema.json",
            "x-bip-module": "toy",
            "$defs": {
                "ToyId": {"description": "An identifier.", "type": "string"},
                "ToyMode": {
                    "description": "A mode. Unknown members fail closed.",
                    "type": "string",
                    "x-bip-closed": True,
                    "x-bip-reserved": ["LATER"],
                    "x-bip-reserved-milestone": "M5",
                    "enum": ["NOW", "LATER"],
                },
                "Toy": {
                    "description": "A toy message.",
                    "type": "object",
                    "properties": {
                        "id": {"$ref": "#/$defs/ToyId"},
                        "mode": {"$ref": "#/$defs/ToyMode"},
                        "tags": {"type": "array", "items": {"type": "string"}},
                        "count": {"type": "integer", "minimum": 0},
                    },
                    "required": ["id", "mode"],
                },
            },
        }
    }
    toy = Contract.__new__(Contract)
    toy.store = jsonschema_mini.SchemaStore(documents)
    toy.modules = {"toy.schema.json": "toy"}
    toy.owner = {name: "toy.schema.json" for name in documents["toy.schema.json"]["$defs"]}
    toy.defs = dict(documents["toy.schema.json"]["$defs"])

    expect(toy.audit() == [], "toy contract passes its own audit")
    rust = render_rust.module(toy, "toy.schema.json")
    expect(GENERATED_MARK in rust, "rust carries the generated marker")
    expect("pub struct ToyId(pub String);" in rust, "rust newtype")
    expect("pub tags: Option<Vec<String>>," in rust, "rust optional array")
    expect("pub count: Option<u64>," in rust, "rust unsigned integer")
    expect("pub id: ToyId," in rust, "rust required field")
    expect('"LATER" => Some(Self::Later),' in rust, "rust from_wire arm")
    expect("pub const RESERVED: &[Self] = &[Self::Later];" in rust, "rust reserved list")
    expect(rust.index("pub struct Toy ") < rust.index("pub struct ToyId"), "sorted defs")

    expect(
        render_rust.module(toy, "toy.schema.json") == rust,
        "rendering is deterministic across runs",
    )
    cxx_version = render_cxx.version({"protocol_version": "7.42"})
    expect(GENERATED_MARK in cxx_version, "C++ version carries the generated marker")
    expect(
        'kBipSchemaVersion[] = "7.42";' in cxx_version,
        "C++ version comes from the owner document",
    )

    store = toy.store
    schema = toy.defs["Toy"]
    expect(
        jsonschema_mini.validate({"id": "a", "mode": "NOW"}, schema, store, "toy.schema.json")
        == [],
        "validator accepts a minimal instance",
    )
    expect(
        jsonschema_mini.validate({"id": "a"}, schema, store, "toy.schema.json") != [],
        "validator rejects a missing required field",
    )
    expect(
        jsonschema_mini.validate(
            {"id": "a", "mode": "SOMEDAY"}, schema, store, "toy.schema.json"
        )
        != [],
        "validator rejects an unknown enumeration member",
    )
    expect(
        jsonschema_mini.validate(
            {"id": "a", "mode": "NOW", "added_later": 7}, schema, store, "toy.schema.json"
        )
        == [],
        "validator accepts an unknown optional field",
    )
    expect(
        jsonschema_mini.validate({"id": "a", "mode": "NOW", "count": -1}, schema, store,
                                 "toy.schema.json") != [],
        "validator enforces minimum",
    )
    try:
        jsonschema_mini.SchemaStore({"bad.schema.json": {"multipleOf": 3}})
        expect(False, "unsupported keyword must raise")
    except jsonschema_mini.SchemaError:
        expect(True, "unsupported keyword raises")

    # The Mojo comparison, on a file written for the purpose. The real
    # projection is checked by --mojom; what is checked here is that the
    # comparison can fail, because a checker that only ever passes proves
    # nothing about the file it reads.
    import os as _os
    import shutil
    import tempfile

    def mojom_findings(text: str, contract: Contract = toy) -> list[str]:
        handle = tempfile.NamedTemporaryFile(
            "w", suffix=".mojom", delete=False, encoding="utf-8"
        )
        try:
            handle.write(text)
            handle.close()
            return mojom.check(contract, handle.name)
        finally:
            _os.unlink(handle.name)

    exact = """module toy.mojom;
enum ToyMode {
  kNow,
  kLater,
};
struct Toy {
  string id;
  ToyMode mode;
  array<string>? tags;
  uint64? count;
};
"""
    expect(mojom_findings(exact) == [], "an exact projection has no findings")
    expect(
        any("kSomeday" in f for f in mojom_findings(exact.replace("  kLater,", "  kLater,\n  kSomeday,"))),
        "an extra Mojo enumeration member is a finding",
    )
    expect(
        any("kLater" in f for f in mojom_findings(exact.replace("  kLater,\n", ""))),
        "a missing Mojo enumeration member is a finding",
    )
    expect(
        any("different order" in f for f in mojom_findings(
            exact.replace("  kNow,\n  kLater,", "  kLater,\n  kNow,")
        )),
        "reordering a closed enumeration is a finding",
    )
    expect(
        any("tags" in f for f in mojom_findings(exact.replace("array<string>? tags", "array<string> tags"))),
        "dropping the nullability of an optional field is a finding",
    )
    expect(
        any("Extra" in f and "bip-transport-only" in f for f in mojom_findings(
            exact + "struct Extra {\n  string a;\n};\n"
        )),
        "an unannotated extra Mojo struct is a finding",
    )
    expect(
        mojom_findings(exact + "// bip-transport-only: a reply message\nstruct Extra {\n  string a;\n};\n") == [],
        "an annotated extra Mojo struct is accepted",
    )
    expect(
        any("Toy" in f for f in mojom_findings(exact.replace("struct Toy {", "// bip-transport-only: wrong\nstruct Toy {"))),
        "annotating a projected type as transport-only is a finding",
    )
    expect(
        any("Toy" in f for f in mojom_findings(
            "module toy.mojom;\nenum ToyMode {\n  kNow,\n  kLater,\n};\n"
        )),
        "a schema object with no projection and no exemption is a finding",
    )
    expect(
        mojom_findings(
            "// bip-not-projected: Toy — browser-authored\nmodule toy.mojom;\n"
            "enum ToyMode {\n  kNow,\n  kLater,\n};\n"
        ) == [],
        "a listed bip-not-projected definition is accepted",
    )
    expect(
        any("Nonexistent" in f for f in mojom_findings(
            "// bip-not-projected: Nonexistent — nothing\n" + exact
        )),
        "listing a name no schema defines is a finding",
    )

    # x-bip-absent-as-empty: the marker changes both surfaces together.
    empty_documents = {
        "toy.schema.json": {
            "$id": "toy.schema.json",
            "x-bip-module": "toy",
            "$defs": {
                "Bag": {
                    "description": "A bag.",
                    "type": "object",
                    "properties": {
                        "items": {
                            "description": "Its items.",
                            "type": "array",
                            "x-bip-absent-as-empty": True,
                            "items": {"type": "string"},
                        }
                    },
                }
            },
        }
    }
    bag = Contract.__new__(Contract)
    bag.store = jsonschema_mini.SchemaStore(empty_documents)
    bag.modules = {"toy.schema.json": "toy"}
    bag.owner = {"Bag": "toy.schema.json"}
    bag.defs = {"Bag": empty_documents["toy.schema.json"]["$defs"]["Bag"]}
    rust_bag = "\n".join(render_rust.definition(bag, "Bag", set()))
    expect("pub items: Vec<String>," in rust_bag, "absent-as-empty renders a Rust Vec, not an Option")
    expect('skip_serializing_if = "Vec::is_empty"' in rust_bag, "absent-as-empty omits an empty Rust list")
    expect(
        mojom_findings("module toy.mojom;\nstruct Bag {\n  array<string> items;\n};\n", bag) == [],
        "absent-as-empty projects a plain Mojo array",
    )
    expect(
        any("items" in f for f in mojom_findings(
            "module toy.mojom;\nstruct Bag {\n  array<string>? items;\n};\n", bag
        )),
        "absent-as-empty rejects a nullable Mojo array",
    )
    bad = dict(empty_documents["toy.schema.json"]["$defs"]["Bag"])
    bad["required"] = ["items"]
    bag.defs = {"Bag": bad}
    expect(
        any("x-bip-absent-as-empty" in f for f in bag.audit()),
        "absent-as-empty on a required property is a schema finding",
    )

    # The C++ mirror comparison, on headers written for the purpose.
    def cxx_findings(header: str, mojo: str = exact) -> list[str]:
        directory = tempfile.mkdtemp()
        try:
            with open(_os.path.join(directory, "toy.h"), "w", encoding="utf-8") as handle:
                handle.write(header)
            mojo_handle = tempfile.NamedTemporaryFile(
                "w", suffix=".mojom", delete=False, encoding="utf-8"
            )
            try:
                mojo_handle.write(mojo)
                mojo_handle.close()
                declarations, _ = mojom.parse(mojo_handle.name)
                return cxx.check(declarations, (directory,))
            finally:
                _os.unlink(mojo_handle.name)
        finally:
            shutil.rmtree(directory)

    same_name = "enum class ToyMode {\n  kNow,\n  kLater,\n};\n"
    expect(cxx_findings(same_name) == [], "a same-named C++ enumeration that agrees passes")
    expect(
        any("kSomeday" in f for f in cxx_findings(same_name.replace("  kLater,", "  kLater,\n  kSomeday,"))),
        "a same-named C++ enumeration with an extra member is a finding",
    )
    expect(
        any("kNow" in f for f in cxx_findings(same_name.replace("  kNow,\n", ""))),
        "a same-named C++ enumeration missing a member is a finding",
    )
    expect(
        any("different order" in f for f in cxx_findings(
            "enum class ToyMode {\n  kLater,\n  kNow,\n};\n"
        )),
        "a reordered same-named C++ enumeration is a finding",
    )
    expect(
        cxx_findings("// Mirrors mojom::ToyMode.\nenum class ToyState {\n  kNow,\n  kLater,\n};\n") == [],
        "a declared mirror with a different name and matching members passes",
    )
    expect(
        any("no Mojo enumeration has that name" in f for f in cxx_findings(
            "// Mirrors mojom::Absent.\nenum class ToyState {\n  kNow,\n};\n"
        )),
        "a mirror comment naming nothing is a finding",
    )
    expect(
        cxx_findings("// bip-local-vocabulary: narrower on purpose.\n"
                     "enum class ToyMode {\n  kNow,\n};\n") == [],
        "an annotated local vocabulary is exempt from the same-name rule",
    )
    expect(
        any("exempts it from nothing" in f for f in cxx_findings(
            "// bip-local-vocabulary: nothing to exempt.\nenum class Unrelated {\n  kA,\n};\n"
        )),
        "a local-vocabulary annotation with no name collision is a finding",
    )
    expect(
        any("cannot be both" in f for f in cxx_findings(
            "// Mirrors mojom::ToyMode.\n// bip-local-vocabulary: also this.\n"
            "enum class ToyState {\n  kNow,\n};\n"
        )),
        "an enumeration that is both a mirror and a local vocabulary is a finding",
    )

    # Version provenance: x-bip-added-in, the reader view it derives, and the
    # compatibility check built on top of both. The property under test is
    # that the *older* reader's view is wrong in exactly one way, because a
    # pruning bug that removed too much would make every additive step look
    # additive.
    expect(version_tuple("0.2") == (0, 2), "version parses")
    expect(version_tuple("0.10") > version_tuple("0.9"), "minor versions order numerically")
    try:
        version_tuple("1")
        expect(False, "a version that is not major.minor must raise")
    except GeneratorError:
        expect(True, "a version that is not major.minor raises")

    def versioned_contract(added_in: dict, on_object: bool = False) -> Contract:
        adapter = {
            "description": "An adapter. An unknown member is unsupported and must fail closed.",
            "type": "string",
            "x-bip-closed": True,
            "enum": ["EARLY", "LATE"],
        }
        info = {
            "description": "What an endpoint supports.",
            "type": "object",
            "properties": {
                "adapters": {"type": "array", "items": {"$ref": "#/$defs/ToyAdapter"}}
            },
            "required": ["adapters"],
        }
        (info if on_object else adapter)["x-bip-added-in"] = added_in
        documents = {
            "toy.schema.json": {
                "$id": "toy.schema.json",
                "x-bip-module": "toy",
                "$defs": {"ToyAdapter": adapter, "ToyInfo": info},
            }
        }
        built = Contract.__new__(Contract)
        built.store = jsonschema_mini.SchemaStore(documents)
        built.modules = {"toy.schema.json": "toy"}
        built.owner = {name: "toy.schema.json" for name in documents["toy.schema.json"]["$defs"]}
        built.defs = dict(documents["toy.schema.json"]["$defs"])
        return built

    versioned = versioned_contract({"LATE": "0.2"})
    expect(versioned.audit() == [], "x-bip-added-in passes the schema audit")
    expect(
        any("not a member" in f for f in versioned_contract({"NEVER": "0.2"}).audit()),
        "x-bip-added-in naming a value that is not a member is a finding",
    )
    expect(
        any("major.minor" in f for f in versioned_contract({"LATE": "later"}).audit()),
        "x-bip-added-in with an unreadable version is a finding",
    )
    expect(
        any("is a object" in f for f in versioned_contract({"LATE": "0.2"}, True).audit()),
        "x-bip-added-in on a structure is a finding",
    )

    reader_store, pruned = versioned.reader_view("0.1")
    expect(pruned == {"ToyAdapter": ["LATE"]}, "the older reader loses the member added later")
    expect(
        versioned.reader_view("0.2")[1] == {},
        "a reader at the version that added it loses nothing",
    )
    naming_late = {"adapters": ["LATE"]}
    expect(
        jsonschema_mini.validate(
            naming_late, versioned.defs["ToyInfo"], versioned.store, "toy.schema.json"
        )
        == [],
        "the added member is valid at the current version",
    )
    reader_schema, reader_base = reader_store.resolve(
        "toy.schema.json#/$defs/ToyInfo", "toy.schema.json"
    )
    reader_errors = jsonschema_mini.validate(
        naming_late, reader_schema, reader_store, reader_base
    )
    expect(
        any(fixtures.CLOSED_ENUM_ERROR in error for error in reader_errors),
        "the older reader refuses the added member as outside its closed enumeration",
    )
    expect(
        jsonschema_mini.validate(
            {"adapters": ["EARLY"]}, reader_schema, reader_store, reader_base
        )
        == [],
        "the older reader still accepts every member it always had",
    )

    def previous_minor_findings(**overrides) -> list[str]:
        entry = {
            "file": "toy.json",
            "schema": "toy.schema.json",
            "definition": "ToyInfo",
            "expected_verdict": "REJECT_UNSUPPORTED",
            "reader_protocol_version": "0.1",
        }
        entry.update(overrides.pop("entry", {}))
        return fixtures.at_previous_minor(
            entry,
            overrides.pop("instance", naming_late),
            versioned,
            overrides.pop("spoken_version", "0.2"),
            overrides.pop("errors_today", []),
        )

    expect(previous_minor_findings() == [], "a well-formed additive fixture has no findings")
    expect(
        any("REJECT_UNSUPPORTED" in f for f in previous_minor_findings(
            entry={"expected_verdict": "ACCEPT"}
        )),
        "a reader-version fixture claiming another verdict is a finding",
    )
    expect(
        any("not older" in f for f in previous_minor_findings(
            entry={"reader_protocol_version": "0.2"}
        )),
        "a reader version that is not older than this one is a finding",
    )
    expect(
        any("major version" in f for f in previous_minor_findings(
            entry={"reader_protocol_version": "1.0"}
        )),
        "a reader version with a different major version is a finding",
    )
    expect(
        any("accepted it" in f for f in previous_minor_findings(
            instance={"adapters": ["EARLY"]}
        )),
        "a fixture naming no added member proves nothing and is a finding",
    )
    expect(
        any("valid at the current version" in f for f in previous_minor_findings(
            errors_today=["$.adapters: broken"]
        )),
        "a fixture that is already invalid today cannot prove a step additive",
    )

    # fixtures.unlisted — the catalogue read the other way round. Watched
    # failing here, because a rule nobody has seen fire is a rule nobody
    # should trust.
    with tempfile.TemporaryDirectory() as directory:
        def write(name: str) -> None:
            with open(os.path.join(directory, name), "w", encoding="utf-8") as handle:
                handle.write("{}")

        write("index.json")
        write("listed.json")
        write("listed.md")
        expect(
            fixtures.unlisted(directory, {"listed.json"}, "index.json") == [],
            "a directory holding only listed files and their notes is clean",
        )

        write("orphan.json")
        expect(
            any("orphan.json" in f for f in
                fixtures.unlisted(directory, {"listed.json"}, "index.json")),
            "a fixture the catalogue does not name is a finding",
        )

        os.remove(os.path.join(directory, "orphan.json"))
        write("orphan.md")
        expect(
            any("orphan.md" in f for f in
                fixtures.unlisted(directory, {"listed.json"}, "index.json")),
            "a note with no listed sibling message is a finding",
        )
        expect(
            not any(f.startswith("index.json:") for f in
                    fixtures.unlisted(directory, {"listed.json"}, "index.json")),
            "the catalogue never reports itself as unlisted",
        )

    print(f"self-test: {checks} checks passed")
    return 0
