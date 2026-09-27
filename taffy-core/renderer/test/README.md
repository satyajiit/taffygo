# `//taffy/renderer/test`

**Status:** `[Proposed]`
**Implementation status:** `[Current]` **run for the first time on 2026-08-19,
and failing.** Until that day `gn refs` answered "Nothing references this." for
`//taffy/renderer:test_support`, the group that carries `:tests`, so
these twenty cases were in no binary at all. They are now in
`//taffy:taffy_browsertests` — the browser-test binary rather than
`taffy_unittests`, because `content::RenderViewTest` initializes Blink per test
and a unit-test launcher has already initialized it for the process, which is
also why upstream runs its own `RenderViewTest` suites out of
`components_browsertests`.

Running them found two real renderer defects on the first attempt, both fixed:
`snapshot_capability_signals.cc` reached the accessibility cache with the
document at `kVisualUpdatePending`, and `adapters/layout_visibility_adapter.cc`
and `adapters/selection_adapter.cc` took a `WebAXObject` with no `WebAXContext`
alive. Each was a `DCHECK` abort in the renderer, which is a browser crash.

What remains is measured and open: after those fixes the cases still do not
pass — `AdapterTest.CapabilityReportNamesEveryAdapterUnderItsOwnMember` fails an
assertion and the rest of the fixture ends in a `SIGKILL` with no diagnostic
before it. That is the next piece of work here, and it is a real result rather
than an absence of one.
**Owns:** the renderer tests that require a parsed document.
**Does not own:** the invariant tests. Node identity, redaction
classification, capability honesty, action decoding, and delta drop order are
provable without a document and live one directory up, in
`//taffy/renderer:unit_tests`.

## Why the split

Cost, not tidiness. The tests one directory up are the ones a security review
asks about, and they must never be skipped for being slow. What is here can
only be proved against a real renderer:

| File | What only a document can prove |
|---|---|
| `endpoint_test_harness.*` | One endpoint bound to a live main frame, plus the request shapes every test needs |
| `fixture_corpus.*` | Reads `test-fixtures/web/manifest.json` so assertions come from the corpus rather than from a copy |
| `adapter_render_view_test.cc` | Composed-tree projection, virtualized recycling, viewport visibility, table relationships, structured-data corroboration, capability honesty, epoch refusal |
| `redaction_canary_render_view_test.cc` | Zero seeded-secret leakage across every fixture that declares one |

## The corpus is the authority, not a copy

Canary tokens and expected omissions are read from the manifest at run time.
A test with `TAFFYGO-CANARY-PASSWORD-4F1A9C` typed into it would keep passing
after the corpus rotated its canaries — and a rotation is exactly when a leak
would be introduced. The manifest is versioned and immutable for the same
reason.

A missing corpus is a **failure**, never a skip. `FixtureCorpus::Load()`
`CHECK`-fails with the path it expected. A leak test that quietly passes
because it could not find the secrets it was supposed to look for produces
the evidence without the assurance, which is worse than no test.

## What the Chromium track must arrange first

1. **Mount the corpus.** The fork tooling mounts
   `taffy-core` at `src/taffy`;
   `test-fixtures/web` needs the same treatment at
   `src/taffy/test/data/web`. Until it does, every test here fails
   at `Load()` with the expected path in its message.
2. **Confirm the harness callbacks.** `content::RenderViewTest::LoadHTML()`
   synchronicity, `GetMainRenderFrame()`, and whether a `RenderViewTest`
   builds an accessibility tree on demand. If it does not, the
   accessibility-path assertions need a mode enabled explicitly — and until
   they do, the capability report will honestly call that adapter
   unsupported, so the tests fail loudly rather than silently asserting less.
3. **Decide the test origin.** `LoadHTML()` produces an opaque origin, so the
   document metadata adapter's origin comparison reports disagreement. That is
   the honest outcome for this harness; the agreeing case belongs to a browser
   test served from the corpus origins.
