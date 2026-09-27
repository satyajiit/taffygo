# procedure-engine

**Status:** `[Proposed]` — this crate implements decision
0055,
which is accepted at the M3 exit review. This crate validates and describes
procedures; the browser owns their durable storage.

**Implementation status:** `[Current]` The record family, lifecycle, six rules,
phrase catalogue, narrowed tool set, definition codec and replay driver run in
host suites. `core-runtime` captures fresh, committed browser operations and
person handbacks into bounded drafts. The UI receives the exact originating
task, full public starting address and every saved step for review. The browser
commits the draft before the catalogue changes; the person enables its exact
version separately. Restored tasks and private profiles cannot create drafts.

A discovery search can precede the recording boundary. Only search, page reads,
queries and navigation are allowed before a verified public starting page. A
link-derived start needs the exact newly committed browser source locator;
direct navigation uses the executed public address. Search text is not saved.
Once recording starts, every operation is retained or the entire recording is
refused. The supported subset is public navigation, whole-page reads, closed
semantic click/focus/link targets, person handover and browser-owned downloads.

Replay constructs the saved errand's ordered plan, reads missing page evidence,
binds a unique visible enabled target to the complete current browser identity,
and waits for matching committed receipts. Handover waits for its own handback;
a download finishes only after its task-owned browser table reports completion.
Only owned transfers still progressing are polled. A paused, interrupted,
cancelled, missing or truncated result fails with an unverifiable-action reason.
`tests/learned_flow_recording.rs` covers the reviewed definitions and legacy
format, and `tests/learned_flow_replay.rs` drives the full navigation, handback
and download sequence through the normal reducer. These are host results; they
do not establish a live government-site run or measured browser latency.

Fresh field classification still has no production seam, so replayed fills hand
the page to the person. Arbitrary search text, prior-step handles and unsupported
operations remain unavailable to automatic recording. Selecting a saved version
remains an explicit offer and does not establish automatic goal matching.

Owning milestone: M3 (the assistant and workspaces).

## The authority boundary

**This crate describes; it grants nothing.** A procedure narrows the tool set a
task may call and can never widen it, `policy-engine` still decides every step,
and where a procedure came from is a field on the record rather than an input to
any decision.

A matched procedure changes which command is *proposed* next. It changes nothing
about what happens to that command afterwards: every step is a proposal from
`task-engine`, decided by `policy-engine`, minted as a capability, dispatched
under a lease, verified by its postcondition, and recorded by `audit-engine` —
the identical path an unmatched task's step takes. The saving is the model call.
The saving is not the checks.

## One record, not two

A skill is written down. A procedure is arrived at by the assistant completing a
task once. Their origins differ and nothing else does, so `Procedure` is the
only record type, `ProcedureProvenance` is a field on it, and `ProcedureStatus`
is the only lifecycle. Two systems with one set of hazards means every rule is
written twice and enforced once.

## The six structural rules

Decision 0055 section 4. `rules::validate` checks all six when a procedure is
stored and again when it is loaded, and its refusal names the rule that broke.

| Rule | Enforced by |
|---|---|
| 1. No expressions | `StepValue` has no computing variant; a literal carrying a substitution sigil is refused |
| 2. No backward edges | a data reference must name a strictly earlier step; there is no jump to express |
| 3. No new verbs | every verb resolves in `task_engine::tool::REGISTRY`, and its arguments against that row's compiled-in schema |
| 4. Catalogued phrases | a closed catalogue matched by equality after normalization; no regular-expression engine ships |
| 5. `MAX_PROCEDURE_STEPS = 32` | refused at storage, never truncated at replay |
| 6. An unclassified field is handed over | `field::disposition` downgrades a fill to `HandToUser`, and `replay` asks it before anything else a step could do |

`tests/six_structural_rules.rs` names the rules one at a time — two tests where a
rule has two ways to break — and each checks the rule the refusal named rather
than only that something was refused.

## The record format lives in the blob, and this crate checks it

Decision 0091
section 2. `core_skill_version.definition` is a `BLOB` bounded by the table's
own `CHECK` at 65536 bytes, with `step_count` beside it as a column, and
everything about what a procedure *is* lives inside that blob and is
`definition`'s to serialize. That is what makes adding a step kind a change to
one serializer and its round-trip test rather than a journal head-version bump.

The trade the same section names is what the module is for: the database cannot
check anything inside a definition, so this crate checks at store and again at
load. Four properties are worth reading before editing it:

- **No numeric discriminant anywhere.** Every closed vocabulary is written as
  the name it already carries for an audit record and read back through that
  enumeration's own `ALL`, so an unknown name is a refusal that says *which*
  vocabulary it is not in, and reordering an enumeration — including the two
  `bip_types` owns — cannot silently re-read a stored record.
- **The format version is inside the blob.** Version 1 remains readable without
  a source-task association. Version 2 stores that optional association and is
  used for new writes. Unknown versions fail with a named reason.
- **Nothing truncates.** Past the column's bound, `encode` stops and names it;
  `decode` refuses a blob larger than the column could have held, one that ends
  early, and one with anything after it.
- **It does not validate.** `MAX_PROCEDURE_STEPS`, the six rules and the
  registry are `rules::validate`'s. A codec that also enforced rule 5 would be
  the second place that rule is expressed.

## Recording reads the live ledger, and refuses as a whole

The journal deliberately lacks the page facts needed for recording. The live
core combines browser-delivered observations with committed verification events
and handbacks. Manual teaching reaches the same validator through the existing
Core API and Core Service transport.

Three properties are worth reading before editing that module:

- **Values stay bounded and reviewable.** A reference, compiled choice, count,
  flag or closed semantic role and phrase never contains person-entered bytes.
  The one stored address is a same-origin public HTTPS starting URL, rejecting
  credentials, query strings and fragments. Later download destinations are
  resolved from the fresh link in the browser and never enter the definition.
- **An entry is never missing, only undescribed.** `LedgerEntry` has no "skip
  this one", and `Recording::admitted` carries the ledger's own count beside
  the descriptor builder's entries so the two can be caught disagreeing. A
  procedure missing a step is not a shorter procedure: it will be replayed, and
  the gap is where it silently does something other than what the person
  watched happen.
- **All six rules are checked at record time.** Recording is a storage, so
  `rules::validate` runs before a record exists and `SkillRecordError::Structural`
  names the rule that broke.
  `tests/recording_refuses_rather_than_degrades.rs` has one test per rule — two
  where a rule has two ways to break — plus decision 0055's fourth validation
  item: a recording that lost a step produces no procedure.

## The conformance oracle

`tests/procedure_matches_hardcoded_workflow.rs` is decision 0055's first
validation item. One task is driven through `task_engine`'s hard-coded
`next_reviewed_command` and through `replay::next_procedure_command` replaying
`builtin::build_source_table`, and the two must emit an **identical** `Command`
sequence.

Whole commands, never `CommandKind`. Two rows of the agent decision table both
answer `FailTask` and differ only in `FailureReason`; a comparison over kinds
would call those equal, and would call every `ProposeAction` equal to every
other regardless of the tool, the effect identity or the digest a capability is
about to be bound to — which is the entire content of the claim.

The two paths share exactly two things, and both are named where they live:
`task_engine::proposal` canonicalises a proposal's identity, because a
difference there would fail the comparison for a reason nobody is testing; and
`task_engine::template` owns the plan and the labelled gap, because a procedure
record carries no prose at all. Everything else is decided independently on each
side.

Passing does not make replay trusted. It is what lets `task_engine::workflow`'s
hard-coded half be **deleted in a later, separate change** — after the oracle
has been green for a while, not on the strength of one run.

## What this crate deliberately does not do

It stores nothing and calls no model. `recording` decides whether a recording is
a procedure and hands the record back to its caller; it opens no database and
reads no ledger of its own. `replay` proposes a
`Command` and never dispatches one: it never authorises, never widens a scope,
and never reaches a page. There
is no dependency on `model-router`, which is a property rather than an accident:
a procedure that has matched needs no model, and that is the point of having
one.

The replay invariants are worth knowing before editing:

- **The retry ceiling is expressed twice, from different facts.**
  `task_engine::workflow` writes `MAX_READ_ATTEMPTS` down; `replay::action`
  derives its ceiling from the verb's `RecoveryRule`. They agree today and
  nothing in the type system holds them together, so
  `the_two_paths_retry_the_same_number_of_times_and_give_up_together` drives a
  read to failure twice and pins both the second attempt and the giving up. It
  is the only case in the oracle where the attempt number is not 1, so it is
  also the only one that compares the attempt component of an idempotency key.

- **A procedure with no verbs must not narrow.** An empty allowlist means
  "everything this milestone has" to `EffectiveToolSet`, so delegating an empty
  verb list would hand back the widest set the build has from the operation
  whose whole purpose is to shrink one. `narrowing::narrow` refuses instead.
- **`user.handover` survives every narrowing.** Removing an escape is not a
  narrowing: a procedure that omitted the exit would strand every task it ran
  on, refused each remaining tool and then refused the way out, with no way to
  say so to the person whose page it is. `task_engine::tool::is_unconditional`
  is where the two are told apart. `user.ask` is deliberately not protected —
  narrowing it costs a capability and not the exit.
- **A record can need a verb it cannot name.** A recorded fill's value is a
  position in what the person supplies, so the step cannot run until
  `user.request_values` has asked for one — and a recording can never contain
  that step, because recording is built from the browser's live *capability*
  ledger and a call reaching the person spends no capability. The ask is
  therefore derived from the record by `narrowing::required_verbs` rather than
  made unconditional: making it unconditional would hand every narrowed set a
  standing licence to interrupt somebody, which is the capability side of
  decision 0055 section 8's line and not the exit side. `narrow` and
  `replay::next_procedure_command` read the same derivation, so the set a
  consent screen is built from and the set replay checks itself against cannot
  come apart.
- **`builtin::BUILD_SOURCE_TABLE_ID` must equal
  `task_engine::REVIEWED_EFFECT_NAMESPACE`.** It is the effect namespace
  `plan_step_key` names a proposal under, so a mismatch is not a naming
  inconsistency: a task that read its source under the hard-coded path and was
  then finished under the procedure path would mint an identity the browser's
  effect journal had never refused, and read the page again having already been
  told it succeeded. `identifier_matches_the_reviewed_namespace` asserts it.
