# core-runtime

**Implementation status:** `[Current]` This crate is the deterministic Rust
composition for one isolated core utility service per browser profile. It owns
no executor, thread, socket, database, filesystem path, browser object, or
ambient credential. Chromium drives every method on one ordered sequence.

## Profile and task ownership

One `CoreRuntime` owns shared profile services—account protocol, policy, model
routing, audit encoding, storage-domain encoding, generation state, and a
profile-wide pending-operation ledger. It also owns a bounded deterministic map
of independent task reducers. `begin_open_task` commits a fresh seed and
creation record before exposing the reducer; `restore_task` accepts only replay
input. `ProductionTaskFactory` is the shipping path that uses the same
`TaskSeed`, browser-minted persisted task entropy, and reviewed digest adapter
for a fresh reducer and journal replay. `ReducerFactory` remains a portable
test seam.

Each task has its own revision, commit-in-flight state, and recovery flag. A
commit for one task never blocks another task. Operation identifiers are unique
within the profile service generation unless unresolved durable logical work is
being rediscovered after a utility-process restart; completions are always
bound to their owning task and current generation.

## Asynchronous boundary

`begin_submit` applies a pure task transition, independently redacts its audit
record, and emits one bounded transactional storage intent. It does no I/O and
does not release reducer effects. `complete_commit` releases those effects
exactly once only after the browser-owned storage broker reports success.
Failure, deadline, cancellation, disconnect, or an ambiguous consequential
outcome withholds the effects and requires replay from committed state.

Other storage, observation, model, browser-action, and tool work leaves as a
closed `EffectRequest` and returns as exactly one closed `Completion`. Every
envelope carries operation identity, service generation, task revision,
browser-owned deadline, idempotency key, and a bounded payload. No Tokio or
second scheduler is linked.

## Recording and replay

After exact task commits, the runtime feeds the fresh errand recorder with
browser-verified action facts and person hand-back events. Only a completed,
regular-profile task without a pending commit or recovery requirement may
prepare an automatic inactive draft. A separate exact skill storage completion
installs that draft; acceptance then names the displayed version. Restored
tasks and selected saved-procedure runs do not create recordings.

The recorded flow begins at a reviewed public site address. A bounded search
prelude may find it, but no search terms are persisted. After that boundary,
an unsupported or unverified step refuses the whole sequence. The browser
retains destination addresses and download identities; replay binds new ones.
An active saved flow is selected by a current page-bound offer. Goal text alone
does not select a saved flow.

## Account protocol

Portable account state uses fixed endpoint identifiers and bounded form fields,
never arbitrary URLs. Rust constructs PKCE state and verifier from
browser-supplied secure entropy and asks an injected Chromium/BoringSSL
`Sha256Port` for S256; the handwritten reference digest is test-only. Verifier,
authorization code, session, and rotating refresh state remain opaque handles.
Redirect receipts must match the exact registered binding and state. Profile
bootstrap restores only committed handle-based session state; pending flows and
refresh work never survive a service generation.

## Workspace state

The runtime restores complete, versioned workspace snapshots from the browser
single writer and owns their immutable Core API projection. Corrections and
source exclusions are optimistic Rust mutations: the proposed revision stays
invisible until the browser reports the exact physical commit. Markdown and CSV
exports are deterministic Rust artifacts and contain only active, host-labelled
evidence.

For a fresh research task, Rust derives the workspace identity from persisted
task entropy and constructs the initial `WorkspaceSnapshot` from the exact
accepted source set after the consent transition commits. The snapshot is
staged through `WorkspacePersistRequest` with expected revision zero and is not
published until the browser confirms the physical write.

After a verified whole-page DOM read or DOM query, Rust requires the exact tab,
frame, page epoch, graph revision, origin and redaction evidence that produced
the adopted page graph. It then derives stable fact identities and stages the
bounded, already-redacted page values with the task phase in one workspace
update. A rejected or ambiguous write publishes neither change. Image, video
and PDF observation completions now use the same commit path through a closed,
coordinate-bearing media provenance shape; page screenshots are deliberately
excluded. This is host-tested ingestion substrate, not evidence that a product
task has extracted media on a device.

## Model calls that belong to no task

Two paths here compose a model request without opening a task, and both are
deliberate rather than incomplete.

`probe` proves one pasted or stored key with a single tiny call against the
cheapest catalog model
(decision 0083).
Its prompt is a fixed string this crate owns, so the call is content-free.

`completion` finishes a sentence a person is typing in the assistant composer
(decision 0097).
It carries the person's own words, so it is disclosed as content they selected,
and it is spent only on a **stored provider credential**. That was written to
explain why the managed route was unreachable from here by construction rather
than by a check — it was entered with a token the browser minted per call and
had no stored credential to find, so a profile configured only that way produced
no suggestion. Decision
0200
removed that route. The requirement stands unchanged and is now the whole rule:
no stored credential, no suggestion.

Neither path journals anything, enters the task ledger, or produces an audit
record, and neither survives the process. A newer composer request supersedes an
older one and names the effect it displaced, because an effect nobody stops is
an effect that still bills.

## The person's own layer of the catalog

The merged catalog has three layers and this crate fills the third. `provider`
holds the endpoints a person typed and the models their probe found;
`provider_listing` holds the models a connected provider served for their
account; and `composition::user_catalog` projects both into the one document
`MergedCatalog::build` lays over the served overlay. It is rebuilt on every
change rather than held, because a held copy can still describe a provider
somebody removed a moment ago.

A saved endpoint arrives with its models in one write
(decision 0096),
and every path that changes what the catalog says — an accepted served
snapshot, an accepted provider command, an accepted model listing — goes
through one function that rebuilds the merge and re-installs the router's
catalog, the plane's rows and the credential references together. A change one
of them was not told about is invisible rather than wrong, which is the
staleness class
decision 0049
was written about.

A provider that serves its own model list is read by the *same* fail-closed
catalog decoder
(decision 0098):
the answer is shaped into the document the published catalog uses and handed
over unchanged, so a provider cannot describe a model in terms the catalog does
not already have. A listing is fetched only where a credential is on file, one
fetch is in flight at a time, and every refusal — including a success this
build could read nothing in — leaves the models a person could already reach
exactly where they were.

## Authority and physical ownership

[`task-engine`](../task-engine/) proposes and
[`policy-engine`](../../../../security/core/rust/policy-engine/) is the only
grant-minting component. The browser owns actor leases, capability validation
and spending, physical storage, credentials, network, tabs, actions, and worker
launch. Tool output returns as untrusted typed data before any further policy
decision.

`create_profile_service_runtime` constructs the production profile composition
from the compiled catalog and policy, real audit and transaction encoders, the
ordered service clock, the browser's per-session generation counter, and the
required reviewed digest adapter. That counter is **not** persisted and is not
unique across browser restarts — see `ServiceGeneration` in
`src/contract/envelope.rs` before keying anything on it. There are no no-op or
fallback production ports.

Authoritative product specifications are indexed from
`docs/README.md`.

## Verifying

```bash
cargo fmt --all -- --check
cargo clippy -p core-runtime --all-targets --all-features -- -D warnings
cargo test -p core-runtime --locked
```
