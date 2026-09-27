# audit-engine

**Status:** `[Proposed]` — the event shape tracks domain model section 17.1,
which is itself proposed and which the milestone M3 storage work has yet to
confirm against a durable log.

**Implementation status:** `[Current]` This crate contains independent
last-redaction projections, two serializers, and an in-memory append-only
reference log. It contains no physical storage, database, file, retention job,
or deletion worker. `core-runtime` encodes its bounded audit projection inside
the browser-owned asynchronous transactional commit intent.

Owning milestone: M2 (page intelligence). Work package
WP-M2-06.

Authoritative specifications:
domain-model.md sections 17, 19,
and 22; data-and-privacy.md
sections 7.2 and 14; threat-model.md
invariant I-13.

## The authority boundary

**This crate records; it never decides.** Whether an action may happen is
`policy-engine`'s decision. Whether an observation may be taken is
`policy-engine`'s decision. This crate projects a bounded record of that
decision, and it is the last layer of the redaction pipeline while doing so.

Being last is the whole job. It assumes every layer above it failed:

- the payload a caller hands it is untrusted input, including the caller's own
  labelling of what each field contains;
- its field policy, not the caller's intent, decides what may be carried;
- it shares no code with the projection serializers in `policy-engine`, because
  an independent layer that reuses the implementation it is checking is not
  independent. The small URL parser here is duplicated on purpose.

## Product audit is not telemetry

Domain model section 22 separates them, and so does this crate: two record
types, two field policies, and two serializers, with the telemetry one starting
from the envelope again rather than from an audit record.

| | Product audit | Operational telemetry |
|---|---|---|
| Answers | What the assistant did for the user | Whether a component is healthy |
| Carries | Identifiers, enumerated names, counts, flags, normalized origins | Enumerated names, counts, flags |
| Never carries | Page text, prompts, model outputs, file names, values, paths, queries | Any of that, plus every identifier, origin, and timestamp |
| Eligible events | All | Only those whose redaction class is operational |

`TelemetryRecord` is content-free **by construction**: every field it holds is a
`&'static str`, a `u64`, or a `bool`, so there is no field a URL, a title, a
prompt, an output, a file name, or an opaque identifier could travel in
whatever a caller supplies and whatever a later field policy says.

## The four redaction gates

A value reaches a record only by passing all four, and each is independent of
the others:

1. **Field policy** — every field name has a disposition per serializer, and a
   name with no policy is dropped.
2. **Kind agreement** — the field name declares the value kind it carries, so
   page text smuggled through a field that is otherwise emitted is dropped.
3. **Origin reduction** — a URL becomes its normalized origin before anything
   sees it, so a query string cannot carry a token onward.
4. **The scan** — a string that names itself a secret is replaced with a fixed
   marker; an identifier too long to be one is dropped; and an identifier that
   is *structured* rather than opaque — long, mixing letters and digits, and
   carrying more separators than a namespace prefix needs — is replaced too,
   because an opaque identifier is one token and a value with internal
   structure is something else wearing an identifier's field.

Field names that are never eligible — a page title, a prompt, a model output, a
file name, selected text — exist in the vocabulary on purpose, so that refusing
them is a table a test can walk rather than an absence nobody notices.

## What this crate deliberately does not do

It decides nothing and performs nothing. It issues no capability, holds no
lease, and reaches no page. It also stores nothing durably: the only log
implementation here is an in-memory reference model. The browser storage
broker is the only physical writer and owns retention, deletion, and export.

## The journal

The reference `AppendOnlyLog` has one mutating method. There is no update, no delete, no
truncate, and no accessor that hands out a mutable event, so append-only is a
property of the API rather than a rule somebody has to follow. Compaction is
deliberately absent: erasing audit records still required by retention is
forbidden, and an operation that cannot be asked for cannot be asked for
wrongly.

In the reference model, the journal — not the caller — assigns the event identifier, the wall-clock
time, the per-stream sequence, and the aggregate revision. An append carries an
expected revision and is refused when it disagrees, so two writers conflict
visibly instead of one silently overwriting the other.

## Verifying

```bash
cargo fmt --all
cargo clippy -p audit-engine --all-targets -- -D warnings
cargo test -p audit-engine --locked
```

| Test file | Proves |
|---|---|
| `tests/canary_audit.rs` | Canaries fed straight into the audit API — as summaries, prompts, outputs, file names, query strings, identifiers, and values of the wrong kind — never appear in serialized output |
| `tests/journal_properties.rs` | An earlier event is never changed by a later one; a conflicting append is refused; missing, repeated, and reordered events are detected; a replay reconstructs the projection the live run produced |

Two suites in `task-engine` cover this crate from the outside, because they cross
crate boundaries: `tests/redaction_end_to_end.rs` runs one observation through
all five layers and shows this one refusing to trust the layer before it, and
`tests/seeded_secret_fixtures.rs` drives every canary the ratified web fixture
corpus declares through both serializers, the journal, and the replayed
projection.

The canary suite asserts on the serialized bytes rather than on the record's
fields, because the bytes are what reaches storage.

## Dependencies

`serde` and `bip-types`, plus `serde_json` for tests. Every shipping crate
clears Chromium's vendoring review, so the runtime dependency tree stays near
zero by design (decision
0004).

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../../../../AGENTS.md#documentation-conventions).
