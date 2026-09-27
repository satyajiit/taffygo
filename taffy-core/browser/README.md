# The BIP browser broker and the M1 browser seams

**Status:** `[Proposed]` implementation of the contract in
the protocol specification
and decision 0002
**Implementation status:** `[Current]` this code **compiles, links and runs
its unit suites on a device**. `//taffy/browser:unit_tests` is part
of `taffy_unittests`, which is built for `dev-arm64` and runs green on a
physical Android phone; the code itself ships inside `taffy_public_apk`. So
every claim below about upstream *API shape* is now evidence rather than
expectation — the compiler settled it. What is **not** settled is every claim
about upstream *behaviour*: it is held by the browser tests, and
`:browser_tests` and `:m1_seams_browser_tests` now reach a binary —
`taffy_browsertests`, registered by patch
[0021](../../chromium/patches/0021-register-taffy-browsertests-suite.md)
and run via `./tools/chromium/test` — whose results this file does not
record, and section 1 marks which of its items depend on one. The run is
recorded in
chromium fork and build
section 6, on a developer workstation and a phone rather than a builder that
owns the evidence of record (Open (OD-026)).
**Wire contract:** [taffy-core/contracts/bip](../../taffy-core/contracts/bip/README.md) —
`//taffy/contracts/bip/mojom/page_intelligence.mojom` is its Mojo projection
and the schema wins on any disagreement.

**Where the sources are.** The 2026-08-22 cut moved the protocol broker's own
sources out of this directory. They are now `//taffy/components/intelligence/content`,
whose [README](../components/intelligence/content/README.md) states that
component's boundary, and every reference to one of them below is written as a
full label. What stayed here is the milestone-M1 seam set of section 5, the
Core Service manager, the browser tests for both halves, and the fixture reader
under `test/`; those are written as bare or directory-relative names. The wire
contract moved too: the BIP mojom is `//taffy/contracts/bip/mojom`, and the
interfaces a platform surface calls this directory through are generated with
the rest of `//taffy/contracts/core-api` (decision
0042).

Six subdirectories hold subsystems rather than platforms. `account/` is the
account plane and the one platform seam it drives,
`account/profile_platform_adapter.mojom`, whose Java implementation is the
only Android thing about it. `assets/` is the delivery plane: the browser half
of how the product's own artifacts reach a device. `catalog/` is the served
catalog's browser half (decision
0080):
one anonymous fetch against the account plane's compiled route, and one cache
file written only when the isolated core says a strictly newer snapshot was
accepted. `providerauth/` is the provider sign-in plane (decision
0081):
the broker that runs every network leg of a vendor subscription sign-in —
device authorization, the PKCE exchange behind the Custom Tab the account
plane's redirect correlation returns to, the one refresh, best-effort
revocation — against a compiled vendor table no served data can move, plus
the resolver that answers the model broker's credential question for a
provider that may be subscription-backed. `model/` is the browser half
of a model call — the isolated core holds no socket, so the request leaves it
as a typed effect and goes out from here, carrying a route and a credential
handle the core never names (decision
0052). The
bytes behind that handle are answered by
`TaffyProfilePlatformAdapter.ResolveProviderAccess` through the resolver in
`providerauth/` — Android decides inside its own critical section what a
sealed record may offer right now — not by another JNI entry.
`core_api/` is the browser-owned facade a surface drives and the projection
from its intents onto Core Service commands. `android/` is the JNI edge and
nothing else: the native entry points and the Java they register. The first
six subsystem directories build on every platform this product could ship
on; `android/` asserts `is_android` at GN load and always will.

This file exists for one reader: the engineer on the x86-64 Linux track. It
says what to check, in what order, and what was guessed. The first compile has
happened, so each item below now says whether it was settled by that build or
is still held by a browser test whose result is not recorded here.

## 1. What to verify first, in priority order

The order is by cost of being wrong, not by difficulty. Items 1 and 2 are
settled by compiling; items 6, 7, 10 and 11 have their unit halves settled by
running; items 3, 4, 5, 8, 9 and 12 are held by browser tests — those now
reach `taffy_browsertests`, but no result from them is recorded here, and
that unrecorded half is the single largest hole in this directory's
evidence.

1. **It compiles at all, and the mojom generates.** `[Resolved]`
   `//taffy/contracts/bip/mojom` uses closed enumerations (no `[Extensible]`, no
   `[Default]`) and optional numeric and enum fields, and the pinned milestone
   accepts all of it: the generator runs, the bindings compile, and the
   fallback of a sentinel plus a `has_*` bool is not owed.
2. **The identity primitives exist and mean what this code assumes.**
   `[Resolved by compiling]` Every name below resolves at the pin and the
   headers say what this code assumed; what a compile cannot show is that the
   *lifetimes* behave as assumed, which is item 3's browser tests.
   `content::FrameTreeNodeId`, `RenderFrameHost::GetFrameTreeNodeId`,
   `RenderFrameHost::LifecycleState`, `DocumentUserData`, and
   `WebContentsUserData`. These are load bearing: `FrameId` is keyed by frame
   tree node so it survives a same-frame navigation, and `PageEpoch` is
   `DocumentUserData` so it dies with the document without anyone remembering
   to kill it. If either assumption is wrong, the invalidation table is wrong.
3. **The protocol section 13 table, row by row.** **Open — in
   `taffy_browsertests`; result not recorded here.**
   `page_intelligence_broker_browsertest.cc` has one test per row. Run them
   before anything else that involves an action, because a page epoch that
   survives a navigation it should not is silent: it is not a crash, it is an
   action on the wrong document. The rows most likely to be wrong at a new
   milestone are back/forward cache entry and restore, prerender activation,
   and `RenderFrameHostChanged`.
4. **Stale handles fail closed.** **Open — in `taffy_browsertests`; result
   not recorded here.**
   `PageIntelligenceBrokerBrowserTest.HandleFromARetiredEpochIsRejected` and
   the refusal tests in `action_dispatcher_browsertest.cc`. 100% rejection of
   handles from an invalid page epoch is a required safety property
   (protocol section 17.4), so a flake here is a defect, not a flake.
5. **Nothing reaches VERIFIED without browser-owned evidence.** **Open — in
   `taffy_browsertests`, result not recorded here; the structural half is
   readable today.**
   `postcondition_verifier_browsertest.cc`, and in particular
   `ARendererCrashNeverProducesVerified` and
   `EveryDeclaredEffectMustBeSatisfied`. The structural guarantee is that
   `//taffy/components/intelligence/content/postcondition_evidence.h` has no field for
   what the renderer said, so no check can read one; confirm that no local
   change has added one. Read
   `PostconditionVerifier::Evaluate` top to bottom: `kVerified` is produced at
   exactly one line, and every declared postcondition has to be satisfied
   before it is reached.
6. **The capability is consumed before any renderer command exists.**
   `[Unit half resolved; the reading is still owed]` Read
   `ActionDispatcher::Dispatch` top to bottom and confirm the order is still
   liveness, then `CapabilityLedger::Admit`, then `ResolveNode`, then the
   journal append, then `ExecuteRendererAction`. The unit tests prove the
   ledger's one-use property; only reading proves the ordering.
7. **The journal append happens before the side effect and a failed append
   aborts.** `[Unit half resolved]` `ActionDispatcher::JournalAndDispatch`.
   An unrecorded side effect is one nobody can reconcile afterwards.
8. **Every call that leaves the process is bounded.** `[Unit half resolved;
   the disconnect half is not]` `RendererCallDeadline`, and the two call sites
   that arm it — `SubmitObservation` and
   `ProtocolNegotiator::Query`. A renderer that never answers must still
   produce exactly one terminal result. The unit test proves exactly-once
   against a mock clock; what needs a real build is confirming that the guard
   outlives a pipe disconnect, because the guard's own timer holds no reference
   back to it and the caller is the one keeping it alive.
9. **The delta round trip, renderer to browser to core service.** **Open — result
   not recorded here.** Everything above works with no renderer endpoint at all. The stream does not: it needs
   `//taffy/renderer`'s endpoint bound in the same test binary so
   `Subscribe` can answer, and it is the largest single gap in this
   directory's evidence. Bring it up in this order, because each step's
   failure mode is invisible in the next:
   1. `Subscribe` returns a subscription id and a base revision;
   2. one delta arrives, matches epoch, from-revision and sequence, and is
      delivered;
   3. a deliberately skipped sequence number produces `kSequenceGap`, kills
      the projection and forces a resnapshot;
   4. a subscriber that stops acknowledging climbs the ladder in order —
      optional signals shed, scope reduced, paused, resnapshot — and each step
      arrives as a `BackpressureNotice` whose `dropped_classes` never contains
      a protected class without `resnapshot_required`;
   5. a fresh snapshot rebases the stream and delivery resumes.
10. **Budget clamping.** `[Resolved]` `budget_clamp_unittest.cc` runs green on
    the device. It asserts relationships,
    never literals, so it should pass unchanged when the ceiling values move.
    If it fails, the clamp is broken, not the numbers. The subscription half
    has one axis that widens rather than narrows — the coalescing window,
    which is raised to the endpoint's floor — and it is the one to read twice.
11. **Memory pressure runs the ladder in the right order.** `[Ordering
    resolved; the device mapping is not]`
    `MemoryPressureGovernor` stops streams *before* it announces the stage, and
    never refuses an action. `memory_pressure_governor_unittest.cc` proves the
    ordering against a fake delegate; a real device is what proves the levels
    map to the right stages.
12. **The corpus is mounted.** **Open — result not recorded here.** Both browser tests
    read `test-fixtures/web/manifest.json` through
    `browser/test/bip_fixture_manifest.h`, and `Load()` `CHECK`-fails with the
    path it expected. This directory has its own reader because
    `//taffy/renderer/test` has one for the renderer's questions and
    the two directories may not include each other; the right end state is one
    reader in a neutral `//taffy/test` target that both halves may
    depend on. That move is a build-file change on the Chromium track and is
    the only known duplication in this directory.

## 2. Items flagged VERIFY in the code

Every one is marked with a `VERIFY AT SP-01` or `VERIFY AT SP-04` comment at
the exact line it affects. Grep for `VERIFY AT` to get the current list; this
table is the summary at the time of writing.

Read the table with the build in mind. Every row that asks about a **name, a
type, a macro or a signature** is answered: the file it points at compiles, so
the guess in its middle column was right and the remedy in its right-hand
column is not owed. What the compiler did not answer is the rest — the rows
that ask about a *policy* (URL disclosure, unqualified node-state
postconditions) and
the two rows about the fixture corpus, whose failure mode is a `Load()` at run
time in a browser test — the suites now reach `taffy_browsertests`, and no
result from them is recorded here. The comments themselves are
deliberately left in the source: a signature that resolves at this milestone
is exactly the thing a rebase can take away, which is why they read `VERIFY
AT` rather than `TO DO`.

| Where | What to check | If the guess is wrong |
|---|---|---|
| `//taffy/components/intelligence/content/page_intelligence_broker.h` | `content::FrameTreeNodeId` as a strong type in its own header | Change the include and two map key types |
| `//taffy/components/intelligence/content/page_intelligence_broker.cc` | `WebContents::ForEachRenderFrameHost` signature — `base::FunctionRef` versus a callback, and the `WithAction` variant | Adjust the two traversals |
| `//taffy/components/intelligence/content/page_intelligence_broker.cc` | `RenderFrameHost::LifecycleState` members | Extend `ToContractLifecycle`; unmapped states already fail closed to destroyed |
| `//taffy/components/intelligence/content/frame_observation_endpoint.h` | Whether `DOCUMENT_USER_DATA_KEY_DECL()` still exists | Delete the line, keep the `_IMPL` macro |
| `//taffy/components/intelligence/content/page_intelligence_broker.h` | Whether `WEB_CONTENTS_USER_DATA_KEY_DECL()` needs to come back | Add the line |
| `//taffy/components/intelligence/content/frame_observation_endpoint.cc` | `GetRemoteAssociatedInterfaces()` as the accessor, and per-document associated binding in the renderer | Rebind through whatever the milestone supports |
| `//taffy/contracts/bip/mojom/BUILD.gn` and its `page_intelligence.mojom` | Associated versus ordinary interface — **decided associated** by record 0032 | A change here is now a protocol design review, not a one-line edit |
| `//taffy/components/intelligence/content/action_dispatcher.cc` | `crypto/sha2.h` versus `crypto/hash.h` | One line in `ComputeDigest` |
| `//taffy/components/intelligence/content/action_dispatcher.cc` | `OpenURLParams` and the `OpenURL` signature, which recently gained a navigation-handle callback | One call site |
| `taffy_product_identity.cc` and `product_user_agent_brand.cc` | **Answered.** No existing embedder seam carries a downstream brand — `GetUserAgentMetadata` passes `std::nullopt` as a literal, `branding_file_path` is gated behind a buildflag this build cannot set, and GREASE takes no parameter. Decision 0130 puts the product in the brand list and leaves the user agent string alone | Two upstream hooks calling `AddProductBrand` — the content browser client for the renderer and workers, the client-hints delegate for `Sec-CH-UA` — specified as patch [0005](../../chromium/patches/0005-user-agent-product-token.md) and blocked until the fork-debt bound is back inside budget |
| `taffy_product_identity.cc` | The generated attribution resource path for a downstream Android product target | One constant |
| `BUILD.gn` | How `:browser_tests` reaches a test binary | Prefer a standalone target or `taffy_public_test_apk` over editing an upstream test binary |
| `//taffy/components/intelligence/content/page_intelligence_service_impl.cc` | URL disclosure is hard-coded to origin-only until the sensitivity policy is wired through | Deliberate under-disclosure; widening it needs the policy, not a constant |
| `//taffy/components/intelligence/content/postcondition_verifier.cc` | Whether revision advance plus re-resolution is strong enough for an unqualified node-state postcondition | Reject unqualified postconditions at authorization time instead |
| `//taffy/components/intelligence/content/bip_schema_version.h` | The protocol version is hand-written here because the contract generator has no C++ backend | Emit it from `taffy-core/contracts/bip/codegen` and delete the constant |
| `//taffy/components/intelligence/content/postcondition_verifier.cc` | `DidOpenRequestedURL`'s parameter list and `WebContents::HasOpener()` | One override signature and one call; without them the new-tab postcondition has no browser-owned observer and must be refused at authorization time |
| `//taffy/components/intelligence/content/memory_pressure_governor.cc` | `base::MemoryPressureListener`'s constructor — one callback or two at the pinned milestone | One construction |
| `//taffy/components/intelligence/content/delta_subscription_manager.cc` | `mojo::ReceiverSet::Add` returning a `ReceiverId`, and removing a receiver from inside a disconnect handler | The teardown path already avoids the second case; if `Add` does not return an id, key the removal differently |
| `test/bip_fixture_manifest.cc` | `base::DIR_SRC_TEST_DATA_ROOT` versus the older `base::DIR_SOURCE_ROOT` | One line |
| `BUILD.gn` | Whether the corpus `data` path resolves once the fork tooling mounts `test-fixtures/web` | Both browser tests fail loudly at `Load()` until it does, which is the intended behaviour |

## 3. Open decisions this code takes a position on

Each position is the conservative one, and each is a single place to change
when the decision lands.

- `[Open (OD-029)]` A back/forward cache restore allocates a **new** page
  epoch, so no handle survives the round trip. One branch in
  `PageIntelligenceBroker::DidFinishNavigation`.
- The renderer interface is bound **associated**, because the stale-node
  algorithm depends on observation and action messages keeping their order
  relative to navigation commits. Decided by record
  0032,
  which closes OD-027 and retires patch specification 0006's binder-map half.
- `[Open (OD-031)]` The process budget ceiling is provisional and lives in
  exactly one place, `//taffy/components/intelligence/content/budget_clamp.cc`. Nothing
  else in the repository restates those numbers.
- `[Open (OD-054)]` The renderer's obscured signal is honored **only in the
  refusing direction**: it can block an action, never permit one. The same
  restraint applies to the frame policy's `renderer_reported_hidden` hint and
  to the `kSectionVisible` postcondition.
- `[Open (OD-045)]` A cross-origin child frame enters an observation only when
  the grant names its origin. An empty allowlist admits same-origin children
  and nothing else, so a hidden third-party frame is excluded before anyone
  asks whether it is visible.
  `//taffy/components/intelligence/content/frame_inclusion_policy.cc` is the
  single place that changes.
- `[Open (OD-056)]` Native/device installation evidence remains open. The M5
  form executor is admitted only through exact operation/input matching, live
  action and sensitivity rechecks, actor lease and one-use capability gates,
  journal-before-dispatch, and a browser-owned one-use value reference. No
  model or core-service message carries the field bytes.

## 3a. How the larger classes are split

Every file named in the table below and in the paragraphs after it lives in
`//taffy/components/intelligence/content` rather than here.

The soft 400-line cap of
android-app-architecture section 5
applies here too, and `./tools/check fast` lane `files` enforces it. Nothing in
this directory carries an exemption.

Two classes are larger than one translation unit, and both are split on the
protocol's own boundaries rather than on line count:

| Class | Units | Split on |
|---|---|---|
| `ActionDispatcher` | `action_dispatcher.cc`, `_admission.cc`, `_dispatch.cc`, `_verification.cc` | The numbered steps of the section 12 sequence. A step is the unit a reviewer reads and the unit an ordering guarantee is about |
| `DeltaSubscriptionManager` | `delta_subscription_manager.cc`, `_subscribe.cc`, `_stream.cc` | The three things a subscription does: it is established, it carries messages, and it ends. Messages and pressure stay together because a delta is what raises pressure |

`BipMojomConversions` is one header and two sources, split on the seam the
header already draws: the action and node vocabulary in
`bip_mojom_conversions.cc`, and the negotiation and stream types in
`bip_mojom_negotiation.cc`. The header stays whole because the overload sets
are one vocabulary to a caller, and splitting it would make every caller
include both.

`monotonic_clock.h` exists because seven files had grown a private copy of
"now". Every envelope, notice and result the protocol carries has an
`observed_at_monotonic_ms`, and consumers compare them across messages
produced by different classes; two readings that rounded differently would
make a delta look older than the snapshot it followed. One reader, one file.

## 3b. The targets, and why the split exists

`BUILD.gn` declares one composition group, one library and three smaller
targets beside it, and the difference between the first two is not cosmetic.

| Target | What it holds | Who links it |
|---|---|---|
| `//taffy/browser:browser` | A `group`, no sources | `//chrome/browser` and `//chrome/browser/profiles`, through patches 0006 and 0032 |
| `//taffy/browser:browser_shared` | Every browser-process source that needs only `//content` plus the three profile-ownership headers `DEPS` names | The group, and every content_shell-based `//taffy` test target |
| `//taffy/browser:core_api_command_factory` | The projection from a generated Core API intent onto Core Service commands, which names no platform type at all | The library, and the unit suite that drives it directly |
| `//taffy/browser:profile_platform_mojom` | The platform seam `account/` drives; `generate_java` is honoured only where Java exists | The library, and the JNI edge's Java |
| `//taffy/browser/android:jni_bridges` | The four JNI entry points, and nothing else | The group, on Android only — so libchrome, and any binary that links `//chrome/browser` |

The four bridges name a `//chrome` Android object: `Profile::FromJavaObject`,
and `TabAndroid` through `@JniType("TabAndroid*")` glue. Both are defined
outside anything a content_shell binary links —
`//chrome/browser/profiles:misc` and `//chrome/browser/android:tabs_public` —
so while those four sources sat in the browser library, every binary that is
not libchrome failed to link on exactly two undefined symbols, and
`taffy_browsertests` had therefore never produced a shared library at all.
Their Java peers are in `//chrome/android:chrome_java`, which no native test
APK carries, so the same four sources are also the only ones in this directory
that can make a test process fail at JNI registration rather than at link.

`:browser` has to be the group rather than the library because
`//taffy/browser` is the label the upstream patches name, and it is the only
edge by which any TaffyGo native code reaches libchrome at all. A content_shell-based test target
therefore depends on `:browser_shared` by name; depending on `//taffy/browser`
from one is the mistake this table exists to prevent. The one test target that
still names the group is `//taffy/test/recovery`, and correctly so: it is a
`//chrome/test` browser test that runs inside a chrome binary, which carries
both halves.

Two consequences are worth stating plainly, because the split does not reach
either of them.

`:unit_tests` depends on `//chrome/test:test_support`, which reaches
`//chrome/browser:browser`, which depends on `//taffy/browser`. So
`taffy_unittests` still links and registers all four bridges — not through
`:browser_shared`, but around it. The unit-test binary is free of the chrome
edge only once its tests stop needing the chrome test harness.

And a test that constructs a `Profile` — a `TestingProfile` included — aborts on
Android whatever the target graph looks like, because `Profile::Profile()` calls
`InitJavaObject()` unconditionally (`chrome/browser/profiles/profile.cc`) and
`org.chromium.chrome.browser.profiles.Profile` is in
`//chrome/android:chrome_java`. That is why
`core_service_manager_idle_unittest.cc`,
`core_service_manager_page_inspector_unittest.cc`,
`core_service_manager_task_effect_unittest.cc`,
`core_service_manager_call_model_unittest.cc` and
`task_source_selection_registry_unittest.cc` cannot run on a device from
`taffy_unittests` as written: each one builds a real `TestingProfile` or a
`ChromeRenderViewHostTestHarness`. Every `Profile*` those sources hand to
`CoreServiceManager` and `TaskSourceSelectionRegistry` is used only through
`content::BrowserContext` API, so the way out is a browser context rather than a
harness — the same move `ProfileAccountBroker` already made.

## 4. What is deliberately not here

- **No renderer code.** `//taffy/renderer` owns the adapters and the
  endpoint. The mojom is the only coordination point.
- **No bridge internals, JNI, or Rust linkage.** `CoreServiceManager` owns only
  a generated Mojo remote to the sandboxed profile utility process. Browser
  adapters own storage, page intelligence, network, tabs, credentials, and
  capability spending; none of those objects crosses the service contract.
  The M1 seams in `:m1_seams` remain free of the service path
  (PAR-AI-BR-001).
- **No semantic graph parsing.** Mojo does the deserialization, this code
  checks bounded scalars, and only the generated BIP graph body crosses as
  bounded bytes inside a typed Core Service result. That split is the Rule of
  Two answer for untrusted structure and the
  reason decision 0004
  puts the core in Rust. `//taffy/components/intelligence/content/bip_graph_payload.cc`
  writes only that graph body;
  observation status, frame/document identity, origin, revision, truncation,
  and redaction cross as generated fields rather than a handwritten outer
  frame. The one thing the graph encoder does read is the free text it is about
  to write onward — a node's
  accessible name and an adapter's detail code — because those are renderer
  authored and the renderer's redaction runs inside the sandbox.
  `//taffy/components/intelligence/content/renderer_text_rescan.h` is that second
  layer, and what it removes is added
  to the envelope's suppressed-secret count so a renderer that stopped
  redacting is visible rather than quietly repaired. Reading text for shapes is
  not reading the graph for meaning; the structure is still Rust's to parse.
- **No delta *evidence*, though the delta path is implemented.**
  `DeltaSubscriptionManager` is the browser-side `PageDeltaClient`,
  `DeltaSubscription` owns one projection cursor and queue, and
  `DeltaBackpressurePolicy` is the ladder. What is missing is a round trip
  against a real endpoint: item 9 of the list above. Until it runs, the honest
  statement is that the stream's refusals are tested and its happy path is not.
- **No tab model, and one call into this directory from above.**
  `TaffyPageIntelligenceHost` is the per-tab owner: it creates the broker, the
  service and the three sinks the service needs, and it is created by exactly
  one upstream line
  ([patch 0023](../../chromium/patches/0023-attach-page-intelligence-to-tabs.md))
  in `TabHelpers::AttachTabHelpers`. Which `WebContents` may be observed is
  decided in `taffy_page_intelligence_host.cc` rather than in that upstream
  line, for the reason every entry in this list gives: a policy decision in a
  file the fork does not own is a decision no downstream test covers.
- **No policy engine.** `taffy-core/components/security/core/rust/policy-engine`
  decides what may be authorized;
  this directory consumes the decision, re-checks it against browser-owned
  state, and consumes the capability. A grant arrives through
  a one-shot `ObservationPolicyGrant` projected only after the profile
  capability ledger spends the matching Rust-minted grant.
- **No model distribution of its own, though the model register is here.**
  `profile_model_register.{h,cc}` implements
  `tool_job_resources::ModelArtifactPort`: it maps a `(model_id,
  model_revision)` pair to an artifact this profile already has. What it is
  not is a way for an artifact to arrive — there is exactly one of those, the
  delivery plane in `assets/`, and the register reads the store that plane
  installs into. It used to read a profile-scoped `TaffyModels/manifest.json`
  that nothing has ever written, so it was empty on every device and the
  emptiness read as a missing file rather than as an undelivered artifact;
  decision
  0060
  retires that manifest. Decision
  0101
  completes the replacement: the isolated delivery adapter publishes a
  complete installed-model snapshot, and the browser hashes each exact
  read-only descriptor on a blocking sequence. Only matching kind, format,
  role, length and digest become resolvable; a worker receives the duplicated
  descriptor and no path. The shipping snapshot remains empty because no
  model row exists. The runtime choice itself is M6's beside
  Open (OD-037).
  `profile_tool_handle_store.{h,cc}` is the same shape for
  `ToolHandleBroker::ResolvePort`: it holds a descriptor that was opened when
  a resource entered the profile, hands it out once, and holds nothing today
  because nothing admits one. Decision
  0041
  is the record for both.
- **One installed browser-flow witness.**
  `taffy_browser_effect_source.{h,cc}` implements `BrowserEffectSource` for one
  of the four browser flows: it observes the profile's download system and
  reports an identifier, a media type, a byte count, a directory **class** and
  a lifecycle state. It reports no file name and no URL, and decision
  0062
  is why — a file name is authored by the server and the page, so carrying one
  would walk attacker-chosen text into the verifier's evidence and from there
  into an audit record. `TaffyPageIntelligenceHost` constructs the witness and
  binds it to the browser's download manager. Installing it makes a
  `kBrowserFlowStarted` postcondition observable, but its dispatch watermark
  is not treated as exact download causation by any shipping task action. The
  typed `StartDownload` path instead returns its own Chromium-issued identity;
  the open-window signal is used conservatively to refuse a direct renderer
  download that has no such authority. The development capability profile admits
  `StartDownload` at M7; the candidate profile used by `release-arm64` selects
  accepted M0 and disables delegated task start. The source of that split is
  `taffy-core/build/product-capabilities.json`. Its validator requires the
  candidate to match acceptance exactly: M0–M1 has no delegated task surface,
  while M2–M8 enables the accepted task milestone with its paired policy
  milestone. Advancing either half without the other is invalid. This is also
  the one file in this directory allowed to include `//components/download`
  outside a test; `DEPS` carries the argument.
- **No metrics macros.** Adding a histogram means editing upstream metadata,
  which is a patch. Observability records go to an injected sink instead; see
  `//taffy/components/intelligence/content/observability_recorder.h` for why the
  record types are trivially copyable.

## 5. The milestone-M1 seams

Everything above describes the milestone-M2 protocol broker. The other half of
this directory is work package WP-M1-02: the browser-process seams that make
every Required-at-M1 row of the
browser parity matrix
testable. They are a separate GN target, `:m1_seams`, for the same reason they
are separate files — the two milestones are reviewed separately, and a change
to one should not be read as a change to the other.

Nine seams, each a small single-purpose class:

| Seam | Files | Rows |
|---|---|---|
| Navigation and lifecycle observation | `browser_navigation_record`, `navigation_error_classifier`, `navigation_lifecycle_tracker` | PAR-NAV-001, -002, -004, -006, -007 |
| Tab and session | `tab_record`, `tab_session_registry`, `session_recovery_planner`, `restored_task_record` | PAR-NAV-003, PAR-TAB-001, -003, -004 |
| Address-bar classification | `omnibox_classification`, `omnibox_input_classifier` | PAR-BOX-001 |
| Credential isolation | `credential_boundary`, `oauth_return_facts`, `oauth_continuity_tracker`, and `credential_field_metadata` in `//taffy/components/intelligence/content` | PAR-AUTH-001, -004 |
| Download | `download_record`, `download_state_machine`, `download_command_router`, and the content-free vocabulary both share with the layer below in `//taffy/common/public/taffy_download_facts.h` | PAR-FILE-001 |
| Android lifecycle | `lifecycle_phase`, `lifecycle_continuity_ledger` | PAR-AND-005, -007 |
| Security posture | `security_posture`, `security_posture_assertions`, `security_posture_evaluator`, `security_posture_probe`, `security_posture_report` | PAR-SEC-001, -002, -008 |
| Secret-leak guard | `seeded_secret_corpus` here; `scrubbed_text`, `sensitive_name_list`, `secret_shape_rules`, `secret_shape_scanner`, `scrubbing_serializer` and `renderer_text_rescan` in `//taffy/components/intelligence/content` | PAR-SEC-009 |
| AI-unavailable guarantee | `ai_runtime_state`, `ai_runtime_availability`, `manual_browsing_guarantee` | PAR-AI-BR-001 |

Plus the two seams the previous pass wrote and this one builds on:
`taffy_product_identity` and `taffy_download_intent_router`, which the download
command router routes a retry back through. The shipping default-browser role
flow is Android-owned Compose code and has no unused browser-process proxy.

**PARITY.md is the authority for the mapping**: one row per parity
row, the file, the test, and — for the rows this directory cannot cover — the
reason and the package that can. It also carries the ordered list of what the
Chromium track should bring up first for these seams, and their own
`VERIFY AT SP-01` summary. Section 1 of this file is the equivalent list for
the protocol broker; run whichever half you are compiling.

Four rules the milestone-M1 seams keep that are worth checking in review, because each
is the kind that decays into a comment nobody enforces:

1. **A renderer cannot write a navigation record.** `BrowserNavigationRecord`
   has a private constructor and no setter; the only producer reads a
   `content::NavigationHandle`.
2. **A restored task cannot express a replay.** `RestoredTaskRecord` is
   trivially copyable and size-bounded, so an action payload cannot survive a
   restart to be replayed after one.
3. **A credential value has no representation.** `CredentialValue` is declared
   and never defined, and `CredentialFieldMetadata` cannot hold an owning
   member.
4. **Unscrubbed text cannot reach a diagnostic sink.** `ScrubbedText` has no
   public constructor and `ScrubbingSerializer` is its only friend.

None of the four is a review convention. Each is a compile error.
