# `//taffy/renderer`

**Status:** `[Proposed]` protocol per decision
0002;
validated at SP-04
**Implementation status:** `[Current]` **compiled and unit-tested on a
device.** `//taffy/renderer:renderer` builds into
`taffy_public_apk` and `//taffy/renderer:unit_tests` is part of
`taffy_unittests`, which runs green on a physical Android phone. Stated
plainly, because the distinction is the whole point: that makes the code
*valid C++ against the pinned milestone*, and it does not make this directory
evidence that BIP works. **The endpoint has now been bound to a real
document in the product** — patches 0006 and 0007 install the service binding
and the render frame observer, and a semantic snapshot has been produced from
a real renderer on a physical phone, a run
M0 exit readiness
EE-03 records together with its limits. The browsertest suites that would
verify the claims below reach `taffy_browsertests` and run via
`./tools/chromium/test`; no result from them is recorded here, so every claim
below about behaviour against a live page is still unverified on this page.
Those suites have executed once anywhere in this repository —
`../test/TEST-INDEX.md` section 2.1, 2026-08-20 on the
phone, 219 tests, 195 passed, 24 failed, 23 of them a stable baseline — and
that run predates the 2026-08-22 cut to `//taffy`, so it is a failure baseline
for the next run rather than evidence for anything claimed here. One of its
standing failures, `ShadowAndVirtualizedTest.RecycledRowsDoNotReuseIdentity`,
is a timeout flake in this directory's own subject matter and passes when the
same set is re-run under a filter; knowing that before reading a fresh result
is the difference between a regression and a rerun.
The build is recorded in
chromium fork and build
section 6, on a developer workstation and a phone rather than a builder that
owns the evidence of record (Open (OD-026)).

One property is checked on any host with no checkout at all, because it is
text rather than behaviour: every enumeration here that mirrors a Mojo
enumeration is compared
against it member for member by
`python3 taffy-core/contracts/bip/codegen/generate.py --mojom`, which the `contracts` lane
runs. The three enumerations that share a wire name while meaning something
narrower carry a `// bip-local-vocabulary:` annotation saying why.
**Owns:** work packages WP-M2-03 (renderer endpoint and adapters), the
renderer half of WP-M2-05 (deltas and backpressure), and layer one of
WP-M2-06 (redaction).
**Authority:** Browser Intelligence Protocol
— section numbers throughout the source refer to it.

The renderer half of BIP: it observes the current document and performs the
renderer-local part of an operation the browser process has already
authorized. It runs inside the sandbox and is **trusted for nothing**.

## What is here

| File | What it owns |
|---|---|
| `page_intelligence_endpoint.*` | The `mojom::PageIntelligence` implementation: negotiation, snapshot, subscribe, resolve, execute, cancel |
| `snapshot_builder.*` | One request into one snapshot: run the adapters, merge annotations, narrow to scope, fill the envelope |
| `delta_publisher.*` | One subscription: change classes, batching, backpressure, invalidation |
| `action_command_translator.*` | The one place a wire command becomes an internal instruction |
| `wire_conversions.h`, `wire_enum_conversions.cc` | Enumeration translation, in switches with no default case, so a new member is a compile error |
| `wire_struct_conversions.cc` | Struct translation: the last redaction gate and the URL disclosure rule |
| `page_capabilities.*` | CAP-PI-008: which adapters are present, degraded, or unsupported for this document |
| `observation_limits.*` | The one limits policy, compiled from `observation_limits.json` |
| `taffy_render_frame_observer.*` | One endpoint per document; retires it before the next document exists |
| `semantic_graph.*` | The internal graph types — plain owned data, no Blink, no Mojo |
| `semantic_graph_store.*` | Node identity, retirement, revisions, and the action barrier |
| `field_redaction.*` | Layer one of protocol section 9: which FIELDS are secret-shaped |
| `high_risk_pattern_detector.*` | The bounded detectors of section 9.2: which STRINGS are secret-shaped |
| `node_precondition_checker.*` | The renderer-local half of the protocol section 12 algorithm |
| `renderer_action_executor.*` | The only code here that can change anything |
| `delta_coalescer.*` | Batching and the backpressure drop order of protocol section 10 |
| `dom_mutation_signal_source.*` | The document's own changes as protocol section 5.3 change classes. The only DOM mutation signal there is: `content::RenderFrameObserver` has none, so this sits on `blink::WebDomMutationObserver` (`chromium/patches/0026-report-dom-mutations-to-the-embedder.md`) |
| `adapters/` | The seven adapters and the registry that orders them |
| `test/` | The tests that need a parsed document; see [test/README.md](test/README.md) |

## The seven adapters

One responsibility per module, each with its own header, source, and place in
the ordered set. The list runs in the precedence order of protocol section
7.6 — what a user can perceive and operate first, raw DOM text last — and the
registry runs them in the same order deliberately, because letting a
lower-precedence adapter spend the shared budget first would change the answer
for budget reasons rather than evidence reasons.

| # | Capability | Files | What it produces | What it deliberately does not do |
|---|---|---|---|---|
| 1 | — | `adapters/document_metadata_adapter.*` | The document node, from facts the **browser** stated: epoch, policy identifier, task purpose, lifecycle. Plus the one comparison this process is uniquely placed to make — whether its own view of the origin sits inside the set the broker named | Report a URL, an origin string, frame topology, or a lifecycle of its own. All four would be renderer-authored identity |
| 2 | CAP-PI-003 | `adapters/accessibility_adapter.*` | Roles, computed names, states, and authored `LABELS` / `DESCRIBES` / `CONTROLS` relationships. Also the shadow path | Reimplement composed-tree traversal. It uses the capability Chromium already has |
| 3 | — | `adapters/form_schema_adapter.*` | Control class, label, enabled, read-only, required, sensitivity — for controls inside a form **and outside one** | Read a value. Not even emptiness, not even for an ordinary field. Write anything, ever |
| 4 | CAP-PI-004 | `adapters/structured_data_*` | JSON-LD and microdata, allowlisted properties, each candidate carrying a corroboration verdict against what the page renders | Pick a winner. Where the page shows its own value for a property, the structured claim and the node the reader sees are joined by `SAME_ENTITY_AS` and the status is `CONFLICTED` — two candidates for one property, neither of them dropped |
| 5 | CAP-PI-002 | `adapters/dom_adapter.*` | Structure, bounded text runs, list and table positions, table header relationships, allowlisted typed attributes | Emit raw HTML, scripts, stylesheets, event handlers, or an unbounded attribute map. Cross a shadow or frame boundary. Attach semantics to canvas pixels |
| 6 | CAP-PI-005 | `adapters/selection_adapter.*` | The user's current selection: one region node, `kSelected` on the nodes it covers, and the selected text | Read selected text out of a credential control, or allocate an identity for something no producing adapter described |
| 7 | CAP-PI-006 | `adapters/layout_visibility_adapter.*` | Bounds, viewport intersection, and an occlusion determination from an accessibility hit test | Claim a determination it did not make. See below |

Every adapter returns `kOk`, `kIncomplete`, `kConflicted`, `kUnsupported`, or
`kFailed`, and every one stamps `FieldEvidence` with a source kind, a bounded
source locator, an extraction rule version, and a transformation. There is no
way for an adapter to report a complete answer it did not produce, and a
required adapter that this document cannot support makes the whole snapshot
`UNSUPPORTED` rather than a smaller result that looks whole.

## Five properties, and how each is made structural

### A node id is never reused within a `(FrameId, PageEpoch)`

The counter in `SemanticGraphStore` only increases. `Retire()` erases the
reverse mapping from DOM identity to id *before* recording the retirement, so
the underlying element cannot lead back to a retired id by any path.
`AdvanceIdentityGeneration()` retires every id issued against a source node
**as part of the same operation** — advancing the generation means "the
logical thing living in this element is different now", and a caller that
advanced without retiring would leave the previous row resolvable under its
old id, which is exactly the offscreen-continuity assumption protocol section
8.3 forbids.

Identity keys carry an identity space, so a DOM node id and an accessibility
object id that happen to share a number cannot merge into one node. The
annotating adapters resolve through `Lookup()`, which does not allocate: an
annotation must never mint an identity, because a node with states and no
description is exactly the shape a stale handle has.

`semantic_graph_store_unittest.cc` proves the property over a **generated**
mutation sequence — four thousand operations from a deterministic seed,
interleaving observe, remove, retire-one, and recycle across a pool of DOM
nodes — asserting that no id is ever issued twice, that a retired id never
resolves again whatever happens afterwards, that a replacement element always
gets a new id, and that the revision never decreases. The interesting
interleavings are the ones nobody thinks to write by hand.

Across epochs the guarantee is structural rather than compared: a new document
gets a new endpoint and a new store, and the old store is gone. There is no
code path in which a handle from a retired epoch could resolve, because the
object that could have answered no longer exists.

### A prohibited value is never emitted, because it is never read

Protocol section 9.1 says prohibited values must never be emitted. This
implementation goes one step earlier. `ProhibitedValueFilter::CategoryOf()` is
a question asked *before* a value is touched, and the form adapter contains no
value accessor at all. A structural placeholder says "a control of this class
is here" and carries no value, no suggested value, no length, no emptiness,
and no hash — section 9.1 names a length-derived fingerprint specifically, and
the same reasoning covers every other measurement of a secret.

The signal list is protocol section 9.2's, not the HTML input type: form
semantics, `autocomplete`, ARIA role, authored name, accessible label, origin,
security context, hidden and obscured state, cross-origin embedding, Blink's
own password-field determination, and the policy floor the browser supplied.
Every one of them can raise a classification; none can lower one.

Hints match **tokens**, never raw substrings. That is not cosmetic: "shipping"
contains the letters "pin", and a substring match would emit a delivery
address as a credential placeholder and delete the one field the research
workflow wanted. Being conservative is not an excuse for being wrong, and
`field_redaction_unittest.cc` has a test for exactly that case.

A withheld value is not a sensitivity class of its own. There is no
"prohibited" member in `mojom::Sensitivity`, and inventing one internally
would have meant a lossy mapping at the wire boundary. The control is
described with sensitivity `kCredential` and `ValueKind::kSecretWithheld`,
which says the true thing: the value exists and this code did not read it.

Behind that sit bounded pattern detectors for text that did not come from a
control — payment card numbers validated with the Luhn check, long digit runs,
hexadecimal key material, opaque tokens, and seed phrases. They scan *inside*
a string rather than requiring the whole string to match, which is what the
previous version could not do: "Card ending 4111 1111 1111 1111" walked
straight through it. A match drops the string whole; a mask would still state
the length.

`MayEmitText()` states the last rule once, in one place: a control's value
travels only when the field is demonstrably ordinary, a control's label
travels for everything except credential material, and content travels with
its marking because protocol section 9.3 puts minimisation in the layers that
know the destination. Conflating those three produced a form schema with no
labels in it, which is not a schema.

### "We did not look" is not "not obscured"

The layout adapter reports three outcomes per node, and the third is explicit:
determined visible, determined not visible / off-screen / obscured, or **not
determined** — the probe budget ran out, the node has no element to measure,
or there is no accessibility tree to hit-test against. When nothing determined
occlusion, neither state is asserted and the annotation records that no
determination was made.

`NodePreconditionChecker` then refuses an action whose precondition forbids
occlusion. A check that silently returned "fine" when it could not run would
be worse than no check, because an action policy would read its silence as a
pass — and a caller could get past a visibility gate simply by observing a
document where the probe budget ran out.

Whether any renderer occlusion test is reliable enough to gate a consequential
action on Android is `[Open (OD-054)]`. Until that is settled, the honest
outcome — refusal — is the one that happens by default, and the browser
process re-checks what it can see for itself regardless.

### Every provisional number lives in exactly one file

`observation_limits.json` is the single committed owner of every bound this
component enforces: node and byte budgets, traversal depth, deadlines, delta
queue depth and windows, structured-data parse bounds, form and selection
counts, occlusion probe budget, and every redaction detector threshold.
`tools/generate_observation_limits.py` compiles it into the header the build
uses; the generated header is **not committed**, because committing it would
create the second copy the design exists to prevent.

Call sites cannot invent a bound. The grouped limit types have private
constructors and no public aggregate initialisation, so an adapter that wanted
its own ceiling would have to change `observation_limits.h` — which is a
review, not a patch. A request can narrow and can never widen, and a request
that states nothing gets the ceiling rather than zero, because a defaulted
request read as "zero" would produce an empty extraction that still reported
success.

The generator refuses to run when the configuration and the
`ObservationLimitsValues` struct disagree, so adding a field to one of them is
a build failure rather than a silently defaulted zero. Two commands prove it
on any host, including one that cannot compile anything:

```bash
python3 taffy-core/renderer/tools/generate_observation_limits.py --check
python3 taffy-core/renderer/tools/generate_observation_limits.py --selftest
```

Every value in that file is a placeholder chosen so the code has a bound at
all. None of them is a quality target and none may be quoted as one; the real
numbers come from the supported-device floor and the page corpus and are owned
by `[Open (OD-031)]`. Replacing them is a data change with no code change.

### Fail closed

Every refusal path is the default, and success is the last statement:

- `PreconditionResult` is constructed as `kUnsupported` and is only narrowed
  to `kOk` at the end of `Check()`, so an early return that forgets to set a
  code refuses.
- A precondition this endpoint cannot evaluate — an expected value digest, a
  redirect set, a budget, "no user interaction since lease issue" — is a
  refusal, never an assumption that it holds.
- `Resolve()` distinguishes retired, unknown, stale-revision, and wrong-epoch,
  and every one of them is a refusal. There is no "probably the same node",
  because the only way to produce that answer would be to match on selector,
  text, role, or bounds — exactly what protocol section 12 forbids.
- Unknown `mojom::ActionType` values fail closed in generated Mojo decoding.
  Known form writes still require the exact action advertised by the live node;
  the browser independently rechecks that support and every authority gate
  before it sends the narrowed command.
- The graph revision is re-checked after every step of the precondition walk,
  because each step takes time and the page is allowed to change during it.
- Bounds are compared but never resolved from. A target that moved between
  observation and dispatch is a changed precondition even when its role,
  states and destination are unchanged.

## The capability response, and how every adapter is named

`page_capabilities.*` computes, per document, which adapters are present,
degraded, or unsupported, and why. It is computed from document signals rather
than from the build, because whether the selection adapter can say anything
depends on whether the user has selected something.

All seven adapters map onto a `mojom::AdapterKind` member. Two are named for
the evidence they carry rather than for the wire's word for it: structured
data maps to `kMetadata` — which is what the closed wire enumeration calls
"structured metadata for entity attributes" — and document metadata maps to
`kBrowser`, which is precedence item 1 of protocol section 7.6. The mapping is
exhaustive and lives in one file, `wire_enum_conversions.cc`.

**Selection and layout used to have no member at all**, and the difference is
worth keeping in view because it shaped this directory. `mojom::AdapterKind`
is closed and predates both adapters, so rather than report them under a
member that means something else — a caller that believed it had asked for the
layout adapter and got the DOM adapter would be worse off than one that knows
it cannot ask — their standing reached a caller as a `SnapshotWarning` with a
detail code of the form `capability/<adapter>/<standing>`, and `ProtocolInfo`,
which has no warning channel, could not carry them at all. The additive minor
contract step recorded in
[`taffy-core/contracts/bip/schema/bip.version.json`](../contracts/bip/schema/bip.version.json)
appended `SELECTION` and `LAYOUT`. `wire::ToMojom(AdapterKind)` is now total,
the warning path is deleted rather than left unreachable, and both adapters
are advertised and reported under their own member.

`kSection` scope is **never** advertised. `mojom::SnapshotRequest` carries no
way to name a section — no node handle, no landmark identifier — so honouring
a section-scoped request would mean this endpoint choosing which section the
caller meant. Advertising it and then choosing is precisely the "degraded
result that looks complete" protocol section 6.2 forbids.

## Deltas

Drop order, and it is not negotiable:

```
text/layout  ->  state changes  ->  additions  ->  |  removals
                                                   |  lifecycle
   dropped first --------------------------->      |  never dropped
```

A `static_assert` pins the order to the enum so a reordering stops the build.
When nothing droppable is left, the coalescer invalidates the projection and
asks for a resnapshot rather than dropping a removal: a subscriber that missed
a removal would be holding a handle to something that no longer exists, and
correctness always has the bounded-fresh-snapshot fallback.

Coalescing stops entirely while an action barrier is held (protocol section
5.3), because a batched-away state change is exactly the precondition the
dispatcher is about to re-check. `DeltaPublisher` maps each protocol section
5.3 change class onto a drop class in one switch with no default case, so
adding a change class forces a decision about how droppable it is.

## Coordination with the browser half

Three things need agreement with `//taffy/browser` and
`//taffy/contracts/bip/mojom` (work packages WP-M2-01 and WP-M2-02) before this
code can run:

1. **The page epoch on first bind — settled on 2026-08-20, the first of the
   two resolutions this item offered.** The broker now sends its freshly
   assigned epoch on every snapshot request, first bind included:
   `browser/page_intelligence_service_impl_observation.cc` sets
   `wire->expected_page_epoch` from the endpoint the browser just allocated,
   unconditionally, rather than forwarding the caller's optional. The
   interface did not grow a bind call and the renderer still invents nothing.

   The renderer's refusal stays exactly as it was, and is not now dead code:
   this endpoint still returns `page-epoch-required-on-bind` for a request
   that names no epoch, because the id namespace *is* the epoch. It is
   defence in depth against a browser half that stops stating it, and it is
   covered by `AdapterTest.MissingEpochIsRefusedNotInvented` in
   `renderer/test/adapter_render_view_test.cc`, which drives the endpoint
   directly with `std::nullopt`.
2. **Activation.** `RendererActionExecutor` performs `kActivate` through
   Blink's default-action path and reports `kDispatched`, which is what
   `mojom::RendererActionOutcome` expects. The stricter alternative — the
   renderer returns a hit point and the browser dispatches synthetic input, so
   the renderer holds no primitive that can activate anything — is written up
   in `renderer_action_executor.h` and the enum value for it already exists.
   If SP-04 finds that the default-action path creates or extends user
   activation, that alternative becomes the live path and the mojom needs an
   outcome for it.
3. **Section scope.** `mojom::SnapshotRequest` still carries no way to name a
   section. See verification item 3.

## What the Linux Chromium track must verify, in order

Ordered so that each answer makes the next question cheaper. Items 1–3 are
"does it build and is the contract right"; 4–10 are "is it correct"; 11–14 are
"is it affordable and complete".

1. **`gn gen` and compile with the overlay mounted.** Include paths, the
   generated mojom header, the `//third_party/blink/public:blink_headers`
   dependency, and — new — that the `observation_limits_generated` action runs
   and that `$target_gen_dir` is on the include path for this target, exactly
   as it is for a generated mojom header. Nothing below matters until this
   passes.
2. **The Blink API names.** Every one is marked `VERIFY AT SP-04` in the file
   that uses it; `grep -rn "VERIFY AT SP" .` is the authoritative list. In
   rough order of blast radius: `blink::WebAXObject::HitTest()` and
   `GetBoundsInFrameCoordinates()` and `blink::WebFrameWidget::Size()`
   (`adapters/layout_visibility_adapter.cc`); `blink::WebAXObject::Selection()`
   and `blink::WebLocalFrame::SelectionAsText()`
   (`adapters/selection_adapter.cc`); `blink::WebNode::GetDomNodeId()` and
   `WebNode::To<T>()` (`adapters/dom_adapter.cc`); `blink::WebAXContext`
   construction plus `UpdateAXForAllDocuments()` and `WebAXObject::Serialize()`
   (`adapters/accessibility_adapter.cc`); the control-type accessor and
   `WebInputElement::IsPasswordFieldForAutofill()`
   (`adapters/form_schema_adapter.cc`);
   `WebDocument::GetElementsByHTMLTagName()` and the `WebElementCollection`
   iteration idiom (`adapters/structured_data_claims.cc`);
   `WebAXObject::FromWebDocumentByID()`, `WebNode::FromDomNodeId()`, and
   `WebAXObject::PerformAction()` (`renderer_action_executor.cc`).
3. **One decision, now that the wire change has landed.** The additive minor
   step is done: `SELECTION` and `LAYOUT` are members of
   `mojom::AdapterKind` and of
   `taffy-core/contracts/bip/schema/protocol-info.schema.json`, `wire::ToMojom` is total,
   the capability-warning path is gone, and `ProtocolInfo` advertises both.
   What is still open is whether `SnapshotRequest` should gain a way to name a
   section, or whether `kSection` scope should be removed from the protocol.
   This endpoint refuses it today either way. Nothing here needs a Chromium
   host — `python3 taffy-core/contracts/bip/codegen/generate.py --mojom` is what proves
   the schema, the Mojo projection and the C++ enumerations still agree.
4. **That an endpoint refuses until the browser states a lifecycle.**
   `lifecycle_state_` is an empty `std::optional` until `OnLifecycleChanged`
   fires, and every guard compares it against `kActive`, which an empty
   optional fails. `SnapshotBuilder::Build` refuses with `kDocumentInactive`
   and the detail code `browser-stated-no-lifecycle` for the same reason.
   Check that `taffy_render_frame_observer.cc` actually calls
   `OnLifecycleChanged` before the first observation can arrive on a real
   page — if it does not, every observation is refused and the symptom is a
   product that never answers rather than one that answers wrongly, which is
   the failure worth having but still a failure. There is deliberately no
   default: the previous shape defaulted to `PENDING_COMMIT` and mapped an
   internal "unknown" onto it, which is a renderer inventing a browser-owned
   fact and the coercion protocol section 6.2 forbids.
5. **The `RenderFrameObserver` surface** — `DidCommitProvisionalLoad()`'s
   signature, `FocusedElementChanged()` and `DidObserveLayoutShift()`
   existence and signatures, whether `DidCreateNewDocument()` fires for the
   initial empty document and for same-document navigations, and the
   `AssociatedInterfaceRegistry::AddInterface<T>()` spelling
   (`taffy_render_frame_observer.cc`). Getting the document-creation callback
   wrong means either an endpoint per `about:blank` or a missed epoch
   boundary, and the second is a security bug.
6. **`WebAXObject::FromWebDocumentByID()` after a tree rebuild.** Can it
   return an object belonging to a *different* node than the id was recorded
   against? If it can, the identity generation has to advance on every
   accessibility tree rebuild, not only on the recycle signals it advances on
   today. This is the single most important item on this list.
7. **`ax::mojom::Action::kDoDefault` semantics.** Does it dispatch a real
   event sequence, and does it create or extend user activation? Protocol
   section 11.5 forbids the second. If it does, activation moves to the
   browser (see "Coordination" above).
8. **Zero seeded-secret leakage** across the fixture corpus, into snapshots,
   deltas, logs, crash reports, and analytics. `test/` asserts it, reading the
   canary tokens from the corpus manifest rather than from a copy. This needs
   the corpus mounted at `src/taffy/test/data/web` — see item 12.
9. **Stale handles fail closed, always.** The generated mutation sequence in
   `semantic_graph_store_unittest.cc` covers identity; the equivalent over
   *action* interleavings, on a real document, is what the fork tests add.
10. **The epoch boundary, on the corpus.** Cross-document commit, same-document
   route change, redirect, child-frame navigation, prerender activation,
   back/forward cache entry and restore, renderer crash, tab close. Protocol
   section 13 has the table; whether a back/forward cache restore always
   allocates a new epoch is `[Open (OD-029)]` and this code assumes it does.
11. **The accessibility adapter's cost** on the supported-device floor,
    especially on a page with no existing accessibility tree, and the cost of
    one occlusion hit test. If either is too expensive the adapter becomes
    opt-in per request and the capability report says so honestly rather than
    the extraction quietly getting slower. Related: `[Open (OD-030)]`.
12. **Every provisional limit.** `observation_limits.json` is the whole list
    and the only place any of them appears. The real numbers come from the
    corpus and the device floor and are owned by `[Open (OD-031)]`.
13. **Mount the fixture corpus.** The fork tooling mounts
    `taffy-core` at `src/taffy`;
    `test-fixtures/web` needs the same treatment at
    `src/taffy/test/data/web`. Until it does, every test in `test/`
    fails at `FixtureCorpus::Load()` with the expected path in its message,
    which is correct behaviour: a leak test that skipped because it could not
    find its secrets would produce the evidence without the assurance.
14. **The interface flavor.** Channel-associated, decided by record
    0032.
    The stale-node algorithm depends on observation and action messages keeping
    their order relative to navigation commits, and an associated interface is
    what supplies that ordering.

## How this directory is split, and what the split is on

The soft 400-line cap of
android-app-architecture section 5
applies here, and `./tools/check fast` lane `files` enforces it: over the cap
without an entry in `tools/check.d/file-size-exemptions.tsv` is a failure.
Nothing in this directory has an entry.

Every split below is on a responsibility seam, not on a line count. The test
is whether the two halves can change independently:

| Unit | What it owns |
|---|---|
| `wire_enum_conversions.cc` | Evidence and endpoint vocabulary: where a value came from, which adapter produced it, how sensitive it is, what the endpoint reports about itself |
| `wire_graph_enum_conversions.cc` | The semantic-graph vocabulary: role, state, relationship, value kind, attribute name. This is the vocabulary a model reads the page in |
| `wire_action_enum_conversions.cc` | Action types and renderer-side precondition codes. Apart from the rest because a silent fall-through here would authorize something rather than describe something |
| `field_redaction_signals.{h,cc}` | The signal vocabularies and the matching. Policy a reviewer audits |
| `field_redaction.cc` | The decision made from them: what a field is, how sensitive, and what a prohibited field is replaced with |
| `snapshot_capability_signals.{h,cc}` | What this document can be observed for, and what a consumer is told when an adapter could not deliver |
| `snapshot_builder.cc` | One request into one snapshot: envelope, scope narrowing, adapter loop |
| `adapters/dom_role_mapping.{h,cc}` | What an HTML element *is*, according to the document and nothing else |
| `adapters/dom_adapter.cc` | The traversal and its bookkeeping |
| `adapters/structured_data_vocabulary.{h,cc}` | The allowlist of properties a page may publish into a model's context |
| `adapters/structured_data_claims.{h,cc}` | Reading the markup: JSON-LD blocks and microdata attributes, every loop bounded before it starts and every skip recorded |
| `adapters/structured_data_corroboration.{h,cc}` | Whether a machine-readable claim is corroborated, contradicted, or merely unseen against what the user can perceive |
| `adapters/structured_data_adapter.cc` | Judge, then emit what the three above allowed |
| `semantic_graph.h` | The internal type mirror: one declaration per wire type, and its value is that it is exhaustive |

Comment density is high throughout on purpose: this code is read far more
often on a host that cannot build it than on the one Linux workstation that
can, so the reasoning has to survive in the file rather than in a build log.
The cap is measured in lines of *code* for exactly that reason
— `tools/lib/code_lines.py` excludes comments, because a cap that counted
prose would be a cap on explanation.

## What this directory deliberately does not do

- It does not decide what is allowed. Capabilities, actor leases, approval,
  and the postcondition verifier are the browser half's, and `DEPS` makes
  reaching for one a build error rather than something a reviewer has to
  notice.
- It does not read a form value, at any sensitivity, at this milestone.
- It writes to a page only through Blink's accessibility action path. The M5
  form operations are exact `SET_TEXT`, `SELECT_OPTION`, `TOGGLE`, and submit
  commands already narrowed by the browser; there is no parallel DOM write
  primitive and the renderer never receives a value reference.
- It does not capture screenshots or infer anything from pixels. For the
  task-only fallback it returns live viewport geometry for every prohibited
  secret, or refuses the request; the browser owns capture and opaque-black
  redaction. Coordinate actions remain excluded (`[Open (OD-012)]`).
- It does not resolve a node by selector, text, ordinal, or coordinates, ever.
- It does not name an accessibility role vocabulary of its own; the mapping is
  `[Open (OD-028)]` until the golden-corpus review.

### Everything currently marked `VERIFY`

`grep -rn "VERIFY AT SP" .` is the authoritative list; it is kept in the file
that depends on the answer rather than duplicated here, so it cannot go stale.
