# Browser Intelligence Protocol contract

**Status:** `[Proposed]` — the wire contract for the protocol described in
browser-intelligence-protocol.md,
pending decision
0002 and its
validation spike SP-04.

**Implementation status:** `[Current]` This directory contains the schema, the
generator, the fixtures, and the checks that hold every other surface to it. It
contains no protocol implementation. The Mojo projection, the renderer
adapters, the browser broker, and the semantic graph all exist in
[`taffy-core/`](../../README.md)
and now **compile**: the mojom generator runs at the pinned milestone, the
generated bindings build into the product APK, and the C++ unit suites over
them run green on a physical Android phone. A BIP message **has** now crossed the
boundary: a semantic snapshot was produced from a real renderer in the browser
process on a physical phone, and
M0 exit readiness EE-03 records
that run together with its limits — no node-targeted action has completed, no
stale document has been rejected in practice, and no postcondition has been
verified. So `--mojom` passing
means the four surfaces agree about what a message is; the build means that
agreement survives a compiler; and the crossing means one message shape has
been exercised end to end — not that the protocol works. The rest of that gap
is the subject of SP-04.

BIP is a semantic contract, not a transport. It rides Chromium's existing Mojo
IPC at existing process boundaries. These schemas describe the meaning and the
shape of the messages; which Mojo interface flavour carries them is
**channel-associated on the frame's own message pipe**, decided by record
0032,
which closes OD-027. That is what makes a page epoch enforceable by transport
ordering rather than by comparison.

## Why this directory exists first

Five consumers bind to these types: the Rust task engine, the policy engine,
the audit engine, Chromium's renderer/browser adapters, and contract tests. If each one
wrote its own idea of a page snapshot, the differences would surface as
security bugs rather than as compile errors. One schema, one generator, one set
of fixtures.

## Layout

```text
taffy-core/contracts/bip/
  README.md            this file
  schema/              the normative contract, hand-written JSON Schema 2020-12
    bip.version.json     the protocol version, and the record of every step
                         it has taken
    identity.schema.json     identifiers, origins, lifetime values, node handle
    protocol-info.schema.json  what one endpoint advertises, and its limits
    snapshot.schema.json     request, snapshot envelope, node, evidence,
                             truncation, redaction summary
    delta.schema.json        delta, invalidation, backpressure notice
    action.schema.json       proposal, envelope, renderer command, result
  golden/              valid instances, each with a note saying what it proves
    index.json           which definition each document instantiates
  compat/              deliberate deviations, each with an expected verdict
    manifest.json        the verdict table
  codegen/
    generate.py          the generator and the fixture checker
    jsonschema_mini.py   a small structural checker, so nothing is installed
```

Generated Rust lands in
[`rust/bip-types/src/generated/`](rust/bip-types/src/generated), inside this
contract's source-owned crate, so Cargo and GN compile the same files without a
build script. **Rust is the only language generated from this schema.**

No Kotlin binding is emitted, and that is a decision rather than an omission.
BIP terminates at the browser-side page intelligence adapter: raw BIP
enumerations, encoded graph bytes, capability references, approval digests and
full URLs stay inside the trusted browser/content layer, and Android consumes
the separate, profile-scoped [Core API](../core-api/README.md) instead — typed
availability, document metadata, coarse claims and a redacted snapshot whose
identifiers are freshly allocated for UI use.
[`mojom/BUILD.gn`](./mojom/BUILD.gn) records the same decision from the other
end, where `generate_java` is absent on purpose. A generated Kotlin view of
this contract would put the vocabulary of the trusted layer in the hands of the
untrusted one, and would do it while looking like a convenience.

## The four surfaces, and the four commands that hold them together

| Surface | Where | Held by |
|---|---|---|
| The schema | `schema/` | `--check` refuses to generate from a schema that fails its own audit |
| Rust | `rust/bip-types/src/generated/` | `--check` fails when the file on disk differs from what the schema produces |
| Mojo | `taffy-core/contracts/bip/mojom/*.mojom` | `--mojom` compares the main interface and imported identity vocabulary definition by definition, field by field, member by member, and requires a written reason for every exception |
| C++ | `taffy-core/{common,components/intelligence/content,renderer,browser}/` | `--mojom` also compares every C++ enumeration that mirrors a Mojo one, by declaration or by name |

```bash
python3 taffy-core/contracts/bip/codegen/generate.py --check      # the bindings are current
python3 taffy-core/contracts/bip/codegen/generate.py --self-test  # the generator's own invariants
python3 taffy-core/contracts/bip/codegen/generate.py --verify     # the golden and compatibility fixtures
python3 taffy-core/contracts/bip/codegen/generate.py --mojom      # the Mojo projection and its C++ mirrors
```

All four run in the `contracts` lane of `./tools/check fast`, on any host with
Python 3 and nothing else.

## Version and compatibility policy

The protocol version is `major.minor` and lives in exactly one file,
[`schema/bip.version.json`](./schema/bip.version.json). No schema document, no
comment, and no line of generated code restates it: the generator reads that
file and emits `PROTOCOL_VERSION` from it.

**Every version step is recorded in that same file**, in its `version_history`
list: which version, whether the step was major or minor, what it added, and
the fixture that proves the claim. Recording it anywhere else would create a
second place to forget. A step whose entry names no evidence has not been
made, however green the gates are.

The rules, from the specification's
compatibility section:

| Change | Version step | What a reader does |
|---|---|---|
| Adds an optional field | minor | Accepts the message, ignores the field |
| Adds an enumeration member | minor | Treats the member as unsupported and fails closed |
| Adds an adapter, scope, or action type | minor | Uses it only if `ProtocolInfo` advertises it |
| Removes or repurposes anything required | major | Rejects the message on the version alone |

Rows two and three are the same rule read from two sides, and the second row
governs: a reader that does not define a member does not quietly ignore it. An
enumeration member is marked with the version it arrived at, in
`x-bip-added-in` beside the enumeration itself, and that marking is what lets
`--verify` reconstruct an older reader's view of the contract from the one
schema on disk and check what that reader actually does. An additive step is
proved additive; it is not asserted to be.

Two rules deserve their own sentence, because getting them wrong is how a
protocol becomes unsafe rather than merely awkward.

**Every enumeration is closed.** A value outside the list is unsupported. It is
never coerced to the nearest known member, never treated as the least
restrictive member, and never silently dropped. Every enumeration's schema
description says this, and `generate.py` refuses to generate from an
enumeration whose description does not. The generated Rust `from_wire`
returns `None`, and that means *stop*: the caller has met a value outside the
closed enumeration and must treat the message as unsupported.

**Accepting is not the same as understanding.** A decoder accepts a minor
version's unknown optional field. A delta subscriber that meets an unknown
field inside a `PageDelta` still throws its projection away and takes a fresh
snapshot, because it cannot prove the projection is still correct. The
`UNKNOWN_DELTA_FIELD` invalidation reason exists for exactly that case.

## What the schema models, and what it does not

Modelled here, following the specification sections on
identity,
observation,
deltas,
and actions,
and the domain model:

- the identity hierarchy down to a node handle, with page epochs and graph
  revisions as opaque and non-orderable values respectively;
- the snapshot request and envelope, the semantic node, per-field evidence with
  preserved conflicts, explicit truncation, and a redaction summary that counts
  what it removed without sampling it;
- deltas, invalidations, and backpressure notices;
- the action proposal, the authorized envelope, the narrow one-use renderer
  command, and the full result taxonomy from
  section 11.7,
  in the order the specification lists it. That order is normative and the
  generator preserves it.

Deliberately not modelled yet, each with the milestone or open decision that
owns it:

- **Numeric limits.** `ProtocolLimits` fixes the field set and no value. Every
  bound is a device-floor and page-corpus measurement owned by
  `[Open (OD-031)]`, advertised at runtime by an endpoint.
- **The Mojo interface shape.** No `.mojom`, no request or response envelope
  for the calls themselves, and no subscription options structure. Interface
  flavour is channel-associated, decided by record
  0032;
  the first M2 slice owns the rest of the shape.
- **The role vocabulary as final.** The initial task-oriented set is here, but
  it needs golden-corpus review under `[Open (OD-028)]`.
- **Media, document-viewer, and vision adapters.** They exist as adapter
  identifiers an endpoint could advertise. No adapter-specific message family
  is defined; those belong to their own milestones.
- **A canonical encoding rule for `ContentDigest`.** The digest binds a
  proposal to its authorization; the exact byte encoding it covers is decided
  with the first implementation, not guessed here.

## The reserved milestone M5 values

`ActionType` carries `SET_TEXT`, `SELECT_OPTION`, `TOGGLE`, and `SUBMIT_FORM`.
`ActionInputKind` carries `TEXT`, `OPTION`, and `TOGGLE_STATE`.
`PostconditionKind` carries `NODE_VALUE_CHANGED` and `BROWSER_FLOW_STARTED`.

None of these are supported today. They are reserved so that generated
bindings carry them and exhaustive deny behaviour can be compile-tested rather
than assumed, which is what
section 11.1
asks for. Three things hold:

1. The schema forbids an endpoint from advertising them. `ProtocolInfo`'s
   `supported_action_types` and a node's `actions` both carry a `not`
   constraint, so a message that advertises a reserved value fails validation.
2. `RendererActionCommand.operation` carries the same constraint, so a reserved
   value cannot reach a renderer through a well-formed message.
3. The production dispatcher denies them. Generated code makes that easy to
   write correctly: Rust exposes `ActionType::RESERVED` and `is_reserved`, so
   a dispatcher denies the whole reserved set exhaustively rather than by
   listing its members a second time.

Protocol representation never implies product authorization. The design behind
any of it is `[Open (OD-056)]`, and it needs its own requirements and
threat-model extension before anything changes.

## Regenerating

The generator is host Python 3 only: no package install, no network, no build
step. Output is deterministic, so `--check` is a real gate.

```bash
python3 taffy-core/contracts/bip/codegen/generate.py --write        # regenerate the Rust bindings
python3 taffy-core/contracts/bip/codegen/generate.py --check        # fail if stale
python3 taffy-core/contracts/bip/codegen/generate.py --verify       # check every fixture
python3 taffy-core/contracts/bip/codegen/generate.py --self-test    # check the generator
cargo check -p bip-types                                 # the Rust must compile
```

`--write` and `--check` first audit the schema itself: every definition needs a
description, every enumeration needs its closed marker and its fail-closed
sentence, every reserved value must be a real member with a milestone, and a
definition name must be unique across the whole contract. A definition the
generator cannot classify is an error, never a silently skipped type.

How a definition becomes a type:

| Schema definition | Rust |
|---|---|
| has `enum` | `enum` with `wire`, `from_wire`, `ALL` |
| has `properties` | `struct`, serde-derived |
| plain scalar | newtype, `#[serde(transparent)]` |

Scalars become newtypes on purpose: a `TabId` should not be assignable where a
`FrameId` belongs. Field names are the wire names verbatim rather than Rust
convention, so a reader can line a field up with the schema without a mapping
table.

## The fixtures

[`golden/`](./golden) holds valid instances covering a static document, a
truncated snapshot, an out-of-process iframe tree, a shadow-tree projection, a
virtualized list, conflicting structured-metadata evidence, a redacted password
field, a selection-scoped snapshot whose selection and layout adapters report
under their own members, a delta, an invalidation, a backpressure notice, a
proposal, an authorized envelope, a renderer command, a verified result, and a
stale-epoch result. Every document has a sibling `.md` note saying what it proves, and
[`golden/index.json`](./golden/index.json) says which definition it
instantiates.

[`compat/`](./compat) holds one deliberate deviation per compatibility rule,
with the verdict a conforming decoder must reach recorded in
[`compat/manifest.json`](./compat/manifest.json): a minor-version message with
unknown optional fields must be accepted, an unknown enumeration member and an
unknown major version must be rejected as unsupported, a removed required field
must be rejected as invalid, an oversized message must be rejected on size
alone, and a malformed frame must be rejected without partial recovery.

One fixture names the protocol version of the consumer that reads it rather
than being read by this build. It is the fixture for an additive step: a
message naming an enumeration member added after that consumer was built must
be valid here, and unsupported there, and unsupported *on that member and
nothing else*. Both halves are checked, because a message that fails an older
reader for some unrelated shape reason would prove nothing about the member,
and a message that no reader ever refuses would prove nothing at all.

`--verify` checks both sets with `jsonschema_mini.py`, a small structural
checker written here rather than installed. It implements only the keywords
these schemas use and raises on any keyword it does not implement, so it can
never pass a constraint by ignoring it.

## Changing the contract

A change to identity lifetime, redaction, supported action semantics, stale
handling, origin or frame scope, capability consumption, verifier rules,
compatibility, or telemetry content is security-sensitive. It needs a decision
record or a protocol design review, matching updates to the
domain model and the
threat model, a golden document for the
new behaviour, and a compatibility fixture for the new failure mode. Adding a
message family without a fixture that proves how it fails is not a complete
change.

Any change that moves the version also needs its entry in
[`schema/bip.version.json`](./schema/bip.version.json)'s `version_history`,
naming the fixture that proves the step is what it claims to be. For a minor
step that is the fixture showing a reader at the previous version failing
closed on what was added; for a major one it is the fixture showing a reader
refusing on the version alone.

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../AGENTS.md#documentation-conventions). Every
`[Open (OD-nnn)]` label above cites its entry in the maintainer's
open-decisions register.
