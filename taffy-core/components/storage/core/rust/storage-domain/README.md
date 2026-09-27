# storage-domain (`taffy-storage`)

**Status:** `[Proposed]` — implements decision
0006, which is
itself proposed pending spike SP-05 evidence on migration, encryption, verified
deletion, and device performance.

**Implementation status:** `[Current]` This portable crate contains schema and
storage-domain types plus deterministic migration, lookup, journal, retention,
and deletion algorithms. Its database implementation is test-only reference
evidence. It contains no filesystem access, profile path, physical writer,
encryption, cross-process service, or Chromium binding.

Owning milestone: M3 (the first workflow). The M6 Library and Memory
aggregates now live under `src/library/` and `src/memory/` as bounded resident
projections with typed browser persist requests; the browser-owned core journal
is their durable physical store. Authoritative
specifications: decisions
0006 and
0122, and the
domain model, with retention classes
from data and privacy.

## The authority boundary

Chromium owns browser data. The browser storage broker is the only physical
writer of assistant data. This crate defines the portable assistant-storage
domain; it is not the broker and owns no database connection.

Cookies, saved credentials, browsing history, downloads, site storage, and
session state stay exclusively Chromium's. This database stores opaque
references to browser objects and reacts to browser lifecycle events. The rule
is executable: `deferred::CHROMIUM_OWNED_NAMES` lists the names that must never
appear, and a test walks every table and column in the migrated schema and fails
if one has drifted toward duplicating a profile store.

One browser-owned writer is what makes everything else provable. The isolated
core emits a typed transactional commit intent containing command, events,
audit projection, and effect intents. It releases task effects only after the
browser reports that commit's one typed completion.

## Schema

| Table | Holds |
|---|---|
| `schema_migration` | Applied migrations, their phase, and their checksums |
| `workspace` | The workspace aggregate: profile, title, status, revision, retention, deletion state |
| `source` | Identified material: kind, locators, ownership, sensitivity, retention, deletion state |
| `workspace_source` | Explicit versioned membership, including why a source was excluded or removed |
| `observation` | Immutable captures of a source, with scope, truncation, and redaction summary |
| `provenance_locator` | Inspectable evidence linking a fact to a source and an observation |
| `fact` | Normalized values with classification, confidence, validity, status, and supersession |
| `claim` | Model statements with their validation state |
| `claim_support` | Which facts support which claims |
| `conflict` | Materially disagreeing facts, their reason, and how they were resolved |
| `conflict_fact` | Which facts participate in a conflict |
| `task` | The task aggregate and the snapshots it froze |
| `task_event` | The append-only journal, with an expected-revision constraint |
| `journal_source_projection` | Which events were about which source, so deletion can find them |
| `task_state_projection` | Current task state, rebuildable from the journal |
| `projection_checkpoint` | How far each projection has been rebuilt |
| `model_invocation` | Invocation metadata, separable from payload retention |
| `artifact` | Produced results, their state, and their lineage state |
| `artifact_lineage` | Which sources, facts, and claims an artifact derives from |
| `assistant_config` | The single assistant configuration, versioned |
| `retention_class` | Every retention class and its accountable upper bound |
| `search_document` | One index entry per record, naming the source it came from |
| `search_index` | The full-text index over those entries |
| `deletion_tombstone` | Content-free proof a record was deleted |
| `deletion_receipt` | What one verified deletion actually removed |

Foreign keys are declared for documentation and for hosts that enforce them.
Deletion does not lean on cascade: it is explicit, ordered, and verified,
because a cascade that half fired is indistinguishable from one that never ran.

## Migrations

Eight numbered forward-only migrations bring an empty database to head, each in
one transaction, each carrying its statements, a description, and its
expand-migrate-contract phase. All eight are expand today, because the schema is
new; the phase is recorded rather than inferred so a later change that adds a
column before writing it, and drops one after nothing reads it, is visible in
review and in the ledger.

Editing an applied migration is refused. A forward-only system cannot undo the
difference between what a database has and what a build believes it has, so
`StorageError::MigrationChanged` stops startup instead of reconciling. A
database written by a newer build stops this one for the same reason.

An interrupted upgrade leaves the database at the last version that landed
whole. A test arranges for a migration to fail partway and proves that neither
its ledger row nor the tables its earlier statements created survive, and that
the database still migrates to head afterwards.

## Retention

`retention::RETENTION_CLASSES` is the only place the classes are written down in
this crate, and migration 7 seeds the `retention_class` table from it, so a test
can assert the code and the database cannot disagree.

The class names are decided; their absolute durations are not, and are owed by
OD-042 (and by OD-005 for provider-side retention). Rather than write a
plausible number that would read as a published upper bound, those classes carry
`RetentionBound::Unset` naming the register entry that owes it — visible in the
schema, where an auditor can see exactly which classes still owe a number and
who owes it.

## Deletion

`deletion::delete_source` is one operation in one transaction that verifies
itself. It marks the source so ordinary lookups stop returning it, removes the
citations before the evidence they cite, decides about every fact that cited the
source, empties the index for the source and for every removed fact, relabels
claims that lost support, relabels or removes artifacts whose lineage included
it, removes the journal projections and redacts the events they pointed at,
removes the memberships and the source row, and leaves a content-free tombstone.

Then it checks. If anything it was supposed to remove is still reachable — a
row, an index entry, an index row with no entry behind it — it returns
`StorageError::DeletionUnverified`, the transaction rolls back, and no receipt
is written. A deletion that cannot be verified is a deletion that did not
happen, and a test with a resurrecting trigger proves the check is load bearing.

Two boundaries are reported rather than crossed. A copy the user exported to
another application is outside this authority and is counted, not claimed. Cloud
deletion is never implied by local success, and the local deletion receipt says
`NOT_APPLICABLE` rather than claiming a remote tombstone was sent. That is now
the only answer it can give: decision
0200
leaves no host of this project's for a record to have reached.

The journal is relabeled, not erased. That a source was used is what makes an
audit trustworthy; what was said about it is what the user asked to remove.

## Retrieval

Structured lookup plus full text. No vector index is created and none is
anticipated: an approximate-nearest-neighbour engine is adopted only if the
benchmark proves full text insufficient (OD-038), and building the schema around
one first would make that decision for everyone.

Search terms are treated as text, not as query syntax, so a value copied out of
a page cannot become an expression. Results are returned in identity order;
ranking belongs above this layer, where it can be evaluated against the
benchmark.

The resident Memory projection keeps eight lexical query candidate lists in a
bounded least-recently-used cache.
Only record identities are copied into each candidate list; the current record,
workspace, sensitivity and expiry are checked again on every read before the
result limit is applied. Successful restore and durable mutations clear the
cache, including its query terms. Pending and rejected mutations keep the
committed search view. This cache stays inside one profile generation and is
never written to storage or used to grant access. `tests/memory_search_cache.rs`
exercises expiry, workspace and sensitivity boundaries, durable invalidation,
and unchanged reads while a mutation is pending. This is lexical candidate
caching; it is not a vector index or evidence of retrieval quality on live pages.

## Encrypted-backup planning

`backup::prepare_backup_manifest` accepts only the eight closed record kinds
from decision 0122. It checks the complete count and byte shape first, rejects
duplicate identities, sorts records by kind and stable identity, and returns
the one canonical encrypted-manifest plaintext plus the exact source order in
which the browser must stage payload bytes. The snapshot digest binds that
order, every schema revision, every plaintext length and every plaintext
digest. Account sessions, credentials, live work and downloaded artifacts have
no record-kind variant and therefore cannot be presented to this interface.

On restore, `backup::open_backup_manifest` decodes and verifies that canonical
manifest. It exposes only the bounded record-length layout needed to hash a
decrypted staging file. A restore plan cannot be produced until an observation
for every staged record range matches the authenticated length and digest,
including the content-free tombstone sentinel. The module contains no cipher,
path, file handle, destination writer or Android integration. The browser's
reviewed AES/HKDF archive adapter owns cryptography and physical I/O; a Core
Service protocol and the Android document adapter still have to connect these
two implemented sides before this is a product path.

## Backend

`backend::Connection`, `backend::Transaction`, and `backend::Executor` are
portable reference interfaces for parameterized statements and rows of
scalars. No production implementation is linked into this crate. Host tests
bind `src/sqlite.rs`, which is compiled only under `cfg(test)`; the browser's
separate storage broker adapts Chromium SQL and remains the physical writer.

The core service never receives a database connection, profile path, raw SQL,
or generic filesystem capability. A shipping crate that linked a second
database engine would also enlarge the trusted dependency closure, which is
exactly what Chromium's vendoring review exists to prevent (decision
0004). The test
library is a development dependency and cannot reach a release build.

There is no floating-point value type. Every quantity this schema stores is
exact, and a value that changes when it round-trips is a value deletion
verification cannot compare.

## Still not created in this reference schema

`deferred::DEFERRED_AGGREGATES` names every aggregate the domain model defines
that this legacy reference schema deliberately does not create, with the
milestone that owns it and the reason: retrieval vectors (M6) and personality
presets (M7). The browser-owned physical sync projection left that list with
the sync module itself. Library and Memory no
longer appear in it either: their portable aggregate code is present, and
`core_service_journal.json` creates their browser-owned durable tables. A test
asserts none of the remaining deferred tables exists, so one cannot appear
without the list changing too.

## Verifying

```bash
cargo fmt --all
cargo clippy -p taffy-storage --all-targets -- -D warnings
cargo test -p taffy-storage --locked
```

The reference tests run against a test-only database and cover: migrating an empty database to
head; migrating a current database as a no-op; an edited migration and a
newer-build database both refused; an interrupted migration leaving a usable
database; a workspace with sources and facts round-tripping; full text finding a
source and then not finding it once deleted; a fact with other evidence
surviving with narrowed lineage; deletion refusing to start before running work
is stopped; a deletion that cannot verify committing nothing; and the schema
containing nothing that duplicates a Chromium store.

## No panics

Shipping domain code runs on the isolated core's ordered Rust sequence, while
physical I/O runs in the browser broker. Every fallible domain operation
returns `error::StorageError`, nothing indexes, and corrupt input fails the
operation rather than escaping the core service's crash boundary.

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../../../../AGENTS.md#documentation-conventions).
