# bip-types

**Status:** `[Proposed]` — the Rust view of the Browser Intelligence Protocol,
tracking a contract that decision
0002 and its
validation spike SP-04 have yet to confirm.

**Implementation status:** `[Current]` This crate contains types and lifetime
rules. It contains no protocol implementation: no Mojo interface, no renderer
adapter, no browser broker, and no semantic graph. Nothing here observes a page
or dispatches an action.

Owning milestone: M2 (page intelligence). Authoritative specification:
browser-intelligence-protocol.md.
Wire contract, generator, and fixtures:
[taffy-core/contracts/bip/](../../README.md).

## What the crate is for

The portable task engine, policy engine, audit engine, core-service composition,
and Chromium page-intelligence adapters bind to this protocol. This crate is
the Rust half of "one schema, one generator, one set of fixtures" — so that a
disagreement about what a page snapshot means surfaces as a compile error rather
than as a security bug.

It carries no authority. Whether an observation may be taken, whether an action
may be dispatched, and what a destination may receive are decisions owned by
`policy-engine`. This crate only gives those decisions a vocabulary they cannot
misspell.

## Generated versus hand-written

`src/generated/` is produced by
[`taffy-core/contracts/bip/codegen/generate.py`](../../codegen/generate.py)
from the JSON Schema documents in `taffy-core/contracts/bip/schema/`. Every message shape,
every closed enumeration, and the protocol version constant live there. **Do not
edit it** — change the schema and regenerate. The generator owns those bytes
exactly, so `pub mod generated;` in `src/lib.rs` carries `#[rustfmt::skip]`:
without it `cargo fmt` rewraps the files and the codegen gate fails on a tree
nobody touched.

Everything else is hand-written and adds what a schema cannot express.

| Module | Adds | Specification |
|---|---|---|
| `src/identity.rs` | Handle construction, node-identifier scoping and allocation, and the ordering rules | Section 5 |
| `src/version.rs` | Parsed protocol versions, compatibility verdicts, and the closed-enumeration decoding policy | Section 6.2 |
| `src/result_code.rs` | Classifiers over the action result taxonomy | Sections 11.7 and 12 |
| `src/sensitivity.rs` | The sensitivity lattice, destination handling, and the never-extract value classes | Section 9 |

Each hand-written module re-exports the generated types it extends, so a
consumer imports one name per concept. The generated wire `ProtocolVersion` — a
string — is re-exported as `identity::WireProtocolVersion` to keep it distinct
from the parsed `version::ProtocolVersion`.

## The rules this crate makes hard to break

- **A page epoch is not orderable.** Section 5.2 forbids inferring ordering
  across frames or sessions, so `PageEpoch` implements neither `Ord` nor
  `PartialOrd` and offers no comparison helper. A test asserts the absence
  rather than trusting it.
- **A graph revision is meaningless outside its scope.** `GraphRevision` is not
  orderable on its own; `ScopedGraphRevision` implements `PartialOrd` and
  returns `None` across scopes.
- **Every enumeration is closed.** An unrecognised wire value decodes to
  `Decoded::Unsupported`, which has no accessor that yields a member. It is
  never coerced to the nearest or least restrictive one.
- **Only `VERIFIED` is success.** Every other result code fails closed, and an
  unrecognised code does not decode at all.
- **Sensitivity only rises.** The lattice offers a join and no removal, so
  combining evidence can never lower a classification.
- **No panics.** The types cross mutually distrusting browser, renderer, and
  isolated core-service boundaries. Functions are total: parsing returns
  `Result`, allocation returns `Option`, and nothing indexes.

## Regenerating

```bash
python3 taffy-core/contracts/bip/codegen/generate.py --write   # regenerate bindings
python3 taffy-core/contracts/bip/codegen/generate.py --check   # fail if stale
python3 taffy-core/contracts/bip/codegen/generate.py --verify   # check every fixture
```

## Verifying

```bash
cargo fmt --all
cargo clippy -p bip-types --all-targets -- -D warnings
cargo test -p bip-types --locked
```

The tests in `tests/` are property tests over generated inputs plus a fixture
gate:

| File | Proves |
|---|---|
| `identity_properties.rs` | A handle from another page epoch is never the same node; an allocation sequence never reuses a node identifier within a frame and epoch |
| `compatibility_properties.rs` | Every closed enumeration in the contract refuses every undeclared value; version verdicts follow the major number |
| `sensitivity_properties.rs` | The lattice laws hold; an unclassified value is never handled least restrictively; a credential is withheld everywhere |
| `golden_round_trip.rs` | Every golden document in `taffy-core/contracts/bip/golden/` survives serde with its meaning intact |

The golden test reads `taffy-core/contracts/bip/golden/index.json` rather than a list
written here, so a document added to the contract without a Rust type to decode
it fails instead of being skipped.

## Dependencies

`serde` only, plus `serde_json` and `proptest` for tests. Every shipping crate
clears Chromium's vendoring review, so the runtime dependency tree stays near
zero by design (decision
0004).

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../../../AGENTS.md#documentation-conventions).
