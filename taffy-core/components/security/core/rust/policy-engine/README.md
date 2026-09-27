# policy-engine

**Status:** `[Proposed]` — the decision surface tracks contracts that decision
0004 and the
milestone M2 exit review have yet to confirm against a real browser.

**Implementation status:** `[Current]` This crate contains decisions. It
contains no browser integration: no Mojo interface, no renderer command, no
storage, and no user interface. Nothing here observes a page or dispatches an
action; it says whether one may be dispatched, and to whom a value may be told.

Owning milestone: M2 (page intelligence). Work packages
WP-M2-04 and WP-M2-06.

## What this crate deliberately does not do

It performs no effect. It writes no journal entry, sends no renderer command,
observes no postcondition, and touches no storage: the four effect steps of the
protocol's action sequence are the browser broker's, and what this crate owns is
the decision about what each of their outcomes means. It also holds no
unforgeable capability material — that is process-local to the browser process,
and the record here is metadata and decision evidence rather than reusable
authority.

Authoritative specifications:
browser-intelligence-protocol.md
sections 9, 11, and 12;
domain-model.md sections 12, 19,
and 22; threat-model.md;
data-and-privacy.md sections 7
and 14. The milestone surface comes from roadmap.md
sections 5 and 8.

## The authority boundary

**`task-engine` proposes; this crate decides.** It is the single authorization
point of threat model invariant I-03. No other crate may authorize a browser
effect, widen an origin scope, or decide what a destination is allowed to
receive.

What that means in practice:

- `task-engine` builds proposals and never authority. A proposal is a plan.
- `bip-types` gives every decision a vocabulary it cannot misspell, and carries
  no authority of its own.
- `audit-engine` records what was decided. It redacts again, independently,
  because a recorder that trusts its caller is not a control.
- The browser overlay validates the envelope, consumes the capability in the
  browser process, and sends a narrower one-use renderer command. A renderer
  never receives a capability reference.

The shipping `GrantPolicy` is stateless. It receives a complete typed proposal
plus an immutable `ActorLeaseFact` from the browser's sole registry, validates
every task/profile/tab/document/generation/expiry binding, and may mint one
fully bound `MintedGrant`. The browser ledger may narrow, spend, or refuse that
grant and never widen it. The earlier Rust lease/capability books remain a
non-shipping conformance reference and are not instantiated by the profile
service composition.

## What each module decides

| Module | Decides | Specification |
|---|---|---|
| `action_class` | Which classes of effect a milestone has authorized | Protocol 11.1, roadmap 5 and 8 |
| `risk` | How consequential one action turned out to be, once context is counted | Threat model 8.4 |
| `lease` | Whether the assistant is the actor in a tab, and when it stops being | Domain model 9.3 and 12.3 |
| `approval` | What a person was asked, what they answered, and what makes the answer stop applying | Domain model 12.5, threat model 18.4 |
| `capability` | Whether one exact action is authorized, once, until an expiry | Domain model 12.4, threat model 8.2 |
| `origin` | Whether two origins are the same one, and where a navigation may land | Protocol 5.5, 8.1, 11.4 |
| `precondition` | Whether the world at dispatch time is still the world that was authorized, for steps one to six | Protocol 11.4 and 12 |
| `step` | The ten ordered steps of the stale-node algorithm | Protocol 12 |
| `report` | What the browser reports back at each effect step | Protocol 11.6 and 12 |
| `sequence` | The whole stale-node algorithm as one pure state machine | Protocol 12 |
| `dispatch` | The dispatch path as a type state, where dispatch after revoke does not compile | Protocol 11.3, threat model 8.3 |
| `denial` | Why a proposal was refused, and the release's action surface as a table | Protocol 11.1, threat model 11 |
| `redaction` | What a destination may be told | Protocol 9, privacy 7 and 14 |
| `pipeline` | That the four destinations really are strictly ordered | Protocol 9.3, privacy 7 and 14 |
| `engine` | The moments a task runtime has to ask about | Threat model 8.1 |

`redaction` and `capability` are module directories rather than single files.
`redaction` splits into the destination table, the control signals, the
classifier, the value masker, and the four projections; `capability` splits into
the request-shaped types, the record, and the ledger.

## The rules this crate makes hard to break

- **Deny by default.** The authorized set is an allowlist per milestone. Every
  class of effect exists as a value — including the ones milestone M5 reserves
  and the ones excluded from the release — so refusing them is a table a test
  can walk rather than an absence nobody notices.
- **Lease fact plus capability.** Authority is never issued without a current
  browser-owned lease fact and never outlives it. The browser's one ledger
  spends the grant once; no shipping Rust lease registry competes with it.
- **Take over is synchronous, and racing it does not compile.** When
  `user_took_over` returns, the lease has ended and every undispatched
  capability issued under it has been withdrawn. Authority already in flight is
  reported rather than pretended away. A `DispatchTicket` holds the broker for
  exactly as long as another dispatch could still be started from it, so the
  window in which "dispatch after revoke" could be written is one the borrow
  checker rejects — the crate's compile-fail examples are the test.
- **One answer, one action.** An approval is bound to the exact question it
  answered — action, class, target, document, origin, destination, data classes,
  and risk — and is spent once. Any difference invalidates it, including a
  difference that looks safer, and the approval is marked invalidated in the
  book so the same answer cannot be presented again later.
- **Two gates, not one.** Every proposal passes the milestone surface *and* the
  risk lattice. A class on the ratified surface whose context raised its risk is
  refused, and the refusal names the risk rather than the class.
- **Sensitivity only rises.** Classification is a join over every signal that
  fired. A page that declares a password field ordinary raises it anyway, and no
  operation in the crate removes a classification.
- **Each destination is strictly narrower.** The local observation, the model
  projection, the audit record, and the telemetry record carry progressively
  fewer page-derived fields, and the last carries none. A test walks the pairs
  and asserts strict containment.
- **Unknown values fail closed.** An unrecognized control type, an unrecognized
  `autocomplete` token, a precondition without the operand its kind selects, a
  result code outside the taxonomy, and an origin that will not normalize all
  refuse rather than resolving to the nearest member.
- **No panics, no clocks, no randomness.** Monotonic readings and identifiers
  arrive through `MonotonicClock` and `IdSource`. Parsing returns `Result`,
  lookup returns `Option`, and nothing indexes.

## The decision surface

`GrantPolicy::decide` is the production decision surface: complete proposal,
lease fact, and optional exact approval in; fully bound grant, approval request,
or closed denial out. It holds no registry and performs no dispatch. The legacy
`PolicyEngine`/`DispatchTicket` surface exists only as the conformance reference
used by the property and stale-node suites.

The pure forms are all separately callable: `evaluate_dispatch` for steps one to
six, `StaleNodeSequence` for all ten, `redact_for` for one destination, and
`project_all` for the whole redaction pipeline.

## Verifying

```bash
cargo fmt --all
cargo clippy -p policy-engine --all-targets -- -D warnings
cargo test -p policy-engine --locked
```

| Test file | Proves |
|---|---|
| `tests/action_surface.rs` | Every class off the ratified surface is denied by the production decision function, at every ratified milestone, with the exact reason and result code |
| `tests/stale_node_sequence.rs` | Every branch of every one of the ten ordered steps, with each test named for the step and the failure it proves |
| `tests/dispatch_path.rs` | The whole path runs once for an ordinary link through the type-state ticket, and each decision step refuses with the code the specification names |
| `tests/approval_lifecycle.rs` | Every way an approved question can change invalidates the answer, and over generated interleavings one answer authorizes at most one action |
| `tests/redaction_pipeline.rs` | The four destinations narrow strictly over a corpus and over generated observations, and the checker itself is shown to fail |
| `tests/seeded_secret_corpus.rs` | No canary in a corpus of field shapes reaches a model projection, an audit record, or telemetry |
| `tests/fail_closed_properties.rs` | Expired, revoked, and replayed authority fails closed over generated inputs; unknown wire values never become known members |

The seeded-secret corpus asserts against the debug rendering of a whole
projection rather than a hand-written field list, so a field added to a
projection and forgotten about is still checked. The suite that reads the
ratified fixture corpus lives in `task-engine`, because it drives every
projection and serializer in all three crates at once.

Two of the properties are proved by the compiler rather than by an assertion.
The `dispatch` module carries compile-fail examples showing that a take-over
cannot be requested while a dispatch ticket is alive, that a ticket cannot be
used twice, and that a ticket cannot be cloned; `cargo test` runs them and fails
if any of them starts compiling.

## Dependencies

`bip-types` only, plus `proptest` for tests. Every shipping crate clears
Chromium's vendoring review, so the runtime dependency tree stays near zero by
design (decision
0004).

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../../../../AGENTS.md#documentation-conventions).
