# task-engine

**Status:** `[Proposed]` — the reducer tracks the task machine of domain model
section 9.2 and the journal shape of section 17.1, which is itself proposed and
which the milestone M3 storage work has yet to confirm against a durable log.

**Implementation status:** `[Current]` This crate contains decisions about what
the assistant should try. It contains no execution: no browser, no model call,
no storage, no threads, and no user interface. Nothing here observes a page,
sends a request, or writes a file; it produces an ordered list of effects and
hands them back to its caller.

`core-runtime` applies each accepted transition on Chromium's ordered utility
sequence, emits one typed transactional commit intent, and withholds the
transition's effects until the browser storage broker returns a successful
typed completion. A failed or ambiguous commit requires replay and releases no
effect.

Owning milestone: M3 (the assistant and workspaces). Work package
WP-M0-03.

Authoritative specifications:
domain-model.md sections 9 to 12, 16,
17, and 18, plus its appendix on the tool namespace;
data-and-privacy.md section 15;
decisions 0004
and 0009. The
milestone scope comes from roadmap.md section 6.

## The authority boundary

**This crate proposes; `policy-engine` decides.** Every side effect leaves here
as a proposal, and the decision comes back in as a command. There is no function
in this crate that issues a capability, holds an actor lease, widens an origin
scope, or reaches a page.

What that means in practice:

- an action becomes authority only when a `RecordPolicyDecision` command carries
  the neutral, capability-bound decision returned by `core-runtime`;
- a terminal task turns authority that arrives late into a **cancelled** action,
  whatever the decision said, because a terminal task issues no new actions;
- pausing and stopping hand back `RevokeAuthority` as the **first** effect, and
  every wait on remote work after it.

## What this crate deliberately does not do

It authorizes nothing and performs nothing. There is no function here that
issues a capability, holds an actor lease, widens an origin scope, reaches a
page, calls a model, writes a file, or starts a thread. It also decides nothing
about redaction: what a destination may be told is `policy-engine`'s decision,
and what a durable record may carry is `audit-engine`'s.

## What each module owns

| Module | Owns | Specification |
|---|---|---|
| `task` | The thirteen durable states, the seven displayed ones, the aggregate | Domain model 9.1 to 9.4 |
| `transition` | The table: every state times every command, with its outcome | Domain model 9.2 |
| `command` | The reducer's input alphabet and its replayable envelope | Domain model 18.1, 18.2 |
| `event` | What the journal records about a transition | Domain model 17.1, 17.2 |
| `effect` | What the runtime must do, in an order that revokes before it waits | Decision 0004 |
| `reducer` | The fold, the revision check, and the rebuild from journal | Domain model 9.2, 18.3 |
| `journal` | The append-only record of commands and events | Domain model 17 |
| `plan` | Plans and steps, which explain and authorize nothing | Domain model 11 |
| `template` | What a reviewed template says about itself in a person's words: its plan, and the one gap a bounded observation can leave | Domain model 11 |
| `proposal` | The one canonical identity of a proposal bound to a plan step: its effect key, its digest material, and the hexadecimal both are spelled in | Decision 0055 section 7 |
| `action` | Proposals, their lifecycle, and what an unknown outcome permits | Domain model 12.1, 12.2 |
| `budget` | Limits, where a missing one is a policy default | Domain model 9.4 |
| `tool` | The typed tool namespace and its milestones, what a model is shown about a name, what one task may call, argument validation, outcomes, recovery, and the refusal count | Domain model appendix, decision 0054 |
| `artifact` | Byte-identical Markdown and comma-separated exports | Domain model 16, privacy 15 |

## The rules this crate makes hard to break

- **Two taxonomies, one direction.** The thirteen internal states are never
  shown. `TaskState::display` is the whole of the section 9.2 display table as
  one pure function, and it answers `None` for a task still in setup and
  consent, because those are surfaces rather than task states.
- **Only the reducer commits a transition**, against an expected revision, and
  every transition it commits carries a cause event and a trace identifier.
- **Consent is unskippable.** A draft cannot reach the queue without passing
  through the consent state, because the command that starts it lands there and
  nowhere else.
- **Revoke before waiting.** Entering the settling states returns revocation
  first and the remote wait after it, built that way by a constructor rather
  than by remembering to. On the `policy-engine` side of the same boundary the
  ordering is stronger still: a dispatch ticket holds the broker, so a
  take-over cannot interleave with an undispatched effect at all.
- **Complete means complete.** The state that means success is only reachable
  with a result that has no unmet requirement. A useful result with a labelled
  gap is the partial state, checked here rather than asserted by a caller.
- **A terminal task acts no further.** It proposes nothing and dispatches
  nothing, and authority arriving for it cancels the action.
- **A repeat is not a second run.** A duplicate command returns the original
  result and performs no effect, which is what makes a retry after process death
  safe.
- **An unknown outcome is never quietly retried.** What may follow depends on
  the tool's idempotency class, never on how convenient a retry would be.
- **A missing limit is a policy default.** There is no way to ask the budget
  table for a limit without also supplying the defaults that fill its gaps, so
  there is no path to an unbounded task.
- **Registration is not authorization.** A tool name resolves to the milestone
  that owns it; whether an action may happen is still a per-action decision made
  elsewhere.
- **No panics, no clocks, no randomness.** Time arrives through an injected
  clock and identifiers through an injected source; every function is total.

## The tool namespace

The registry carries every name the domain model's appendix lists, including the
ones no milestone has reached and the two the product excludes by requirement. A
refusal spelled out in the table is enumerable by a test; a refusal expressed by
leaving a name out is invisible until somebody adds it back.

At milestone M3 exactly fifteen names resolve as available: browser-owned
navigation, search, back and forward; the tab namespace; the four protocol-bounded
observation and low-risk interaction names; read-only form inspection; selection
reading; the two deterministic export names; and the two that reach for the
person rather than for the page. Everything else answers with the milestone that
owns it, or with a permanent exclusion.

Handing back and asking are rows in the table like any other, because handing
back is what the assistant reaches for once everything else has been refused and
only a row makes it reachable by the same decision it makes about every other
call. Of the two, only handing back survives a narrowed allowlist: a task that
may not ask can still stop, so narrowing the question costs a capability while
narrowing the exit would cost the way out.

A model is shown a name, one line about it, and a compiled-in parameter table —
no document is parsed and there is no pattern language, so a value that must come
from a fixed set has that set written out. Supplied arguments are accepted or
refused against that table with a named reason, and nothing is converted on the
way: an unknown name, a missing required one and a value of the wrong type each
fail closed. What a call produced is one of three shapes, and a successful one
cannot be empty — an empty result has to say why it is empty, because "we found
none" and "we could not look" must not travel on as the same sentence.

Deferred discovery searches the same compiled registry and the current task's
allowlist. It ranks exact callable names, full names, description phrases and
complete term matches across names, descriptions and parameter metadata, with
stable registry order for ties. Queries are limited to 512 UTF-8 bytes and 16
terms. Discovery can reveal an allowed tool; it cannot authorize a new one.
Unit coverage lives in `tool/effective/discovery/tests.rs`, and
`loop-kernel`'s `tool_activation` test verifies that an activated search result
appears in the next actual model request. No dynamic executable skill content
is loaded by this search.

Nothing a caller may do after a refusal is an unqualified retry. Every
retry-shaped answer names the condition that has to change first, and the map
from every result code to one of the five answers is total, so a new code cannot
inherit a recovery by falling through a default. Identical refusals of the same
call are counted in reducer state rather than discouraged in a prompt, and the
count is re-derived by replaying the journal; three of them end the attempt.
A replay does not refuse a journalled command on that count, or on the budget
ledger, when a later build counts more strictly than the one that admitted it:
those two bounds on conduct are held as history, named in
`Recovery::admitted_as_history`, and meet the next live command at full
strength (decision 0235).

## The export contract

Markdown and comma-separated exports are byte-identical for identical records,
whatever order the caller collected them in, because sources, facts, and evidence
are all sorted into a total order before a byte is written. Every value carries
its citations, and an externally derived fact with no provenance locator is
refused rather than exported uncited.

Every value is treated as hostile input, because a value that came off a page is:
control characters and bidirectional overrides are removed, table and link syntax
is escaped so nothing renders as markup, and a cell a spreadsheet would evaluate
is prefixed so it stays text. The golden files in `tests/golden/` are the
contract; changing them changes what a person receives.

## Verifying

```bash
cargo fmt --all
cargo clippy -p task-engine --all-targets -- -D warnings
cargo test -p task-engine --locked
```

| Test file | Proves |
|---|---|
| `tests/transition_table.rs` | Every state times every command has the outcome the table names, every transition the table names is reachable, a stale revision is refused, and a repeat performs nothing |
| `tests/authority_revocation.rs` | Every command that enters a settling state revokes authority before the runtime waits on anything, checked against a live `policy-engine` broker; and a terminal task acts no further |
| `tests/replay_properties.rs` | Over generated scripts: a rebuild reconstructs the task and the journal, duplicates no dispatched action, invents no completion, and lands on every point the run passed through |
| `tests/replay_across_a_stricter_bound.rs` | A journal written under a looser bound on conduct replays under a stricter one to the state and revision it recorded, names each command it held as history, leaves the next live command refused by the stricter bound, and still refuses a journal it cannot rebuild |
| `tests/process_death_recovery.rs` | After a process dies mid-dispatch: the rebuild is the run, no authority comes back, the in-flight attempt is unknown rather than verified, the dispatch command repeats nothing, and the audit journal replays to the same bytes |
| `tests/redaction_end_to_end.rs` | One observation through all five layers — local context, model projection, audit projection, audit record, telemetry record — each strictly narrower than the last, with the last one shown not to trust the one before it |
| `tests/seeded_secret_fixtures.rs` | Every canary the ratified web fixture corpus declares, driven through every projection and serializer in `policy-engine`, `audit-engine`, and this crate, appearing in none of them |
| `tests/golden_artifacts.rs` | Both exports match their goldens, are order-independent and repeatable, and neutralize a hostile value |

The seeded-secret suite reads its tokens from
`test-fixtures/web/manifest.json` rather than carrying its own list, so a canary
added to the corpus is one this suite starts checking and a canary it cannot
place stops the run. It is the milestone M2 exit criterion "seeded-secret
fixtures produce zero plaintext-secret model, context, log, crash, analytics,
and audit records" as an executable test over the real corpus.

## Dependencies

The shipping dependency graph contains `bip-types` only. Concrete policy,
audit, routing, and storage-domain crates appear solely in integration-test
development dependencies; `serde_json` and `proptest` are also test-only.
Composition belongs to `core-runtime`, so the pure reducer cannot acquire a
platform, vendor, physical-storage, or authority dependency. Every shipping
crate clears Chromium's vendoring review, so the runtime dependency tree stays
near zero by design (decision
0004).

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../../../../AGENTS.md#documentation-conventions).
