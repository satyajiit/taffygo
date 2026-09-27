# `//taffy/test`

**Status:** `[Proposed]` shared test support, the Content Shell suites, and the
Chrome Profile recovery tests
**Implementation status:** `[Current]` every file here is written, and the
directory is now in the build graph. This page once recorded the opposite —
a `BUILD.gn` GN had never parsed, because no label reached it — and that gap
is closed: the dep line in `//taffy/BUILD.gn` is live, and the portable suites
reach `taffy_browsertests`, a Content Shell browser-test binary registered for
Android by patch
[0021](../../chromium/patches/0021-register-taffy-browsertests-suite.md).
The separate `recovery/` source set reaches Chrome's host `browser_tests`
through patch
[0030](../../chromium/patches/0030-register-core-recovery-browsertest.md), and
the dedicated Android `taffy_profile_browsertests` target that patch
[0045](../../chromium/patches/0045-register-taffy-profile-browsertests.md)
declares; it never enters `taffy_browsertests`. All three binaries are driven through
the Chromium build and test tooling, and the overlay's
`tools/check_build_reachability.py` holds every `BUILD.gn` under the overlay
reachable from an explicit product or upstream test root, this one included.
TEST-INDEX.md records what
landing that wiring cost. None of this is evidence that anything passes: a
run's result lives in the run's own output, never here. A test says what it
intends to prove; it does not say it proved it. Section 4 is the remaining
verification list.
**Owning work packages:**
WP-M1-06 — parity and
recovery suites — and
WP-M2-08 —
adversarial validation.
**Authority boundary:** this directory owns test support and Taffy's own
suites. It owns no product behaviour, no policy, and no requirement. The
parity matrix owns every
`PAR-` row, the
protocol specification
owns every invariant, and the
benchmark corpus owns the
fixture list. TEST-INDEX.md is the map from those to the tests
here.

## 1. Why this directory exists

Three reasons, and the first one is structural rather than tidy.

**Both process halves need the same corpus reader, and neither may include the
other.** `//taffy/browser` forbids `+taffy/renderer` and
the reverse is a build error, for the authority reason each `DEPS` file
explains. Before this directory existed there were two readers of
`test-fixtures/web/manifest.json`, written independently, answering different
halves of the same questions. This directory sits below both, so one reader can
serve both without either half depending on the other.

**A browser test needs the corpus served over four separate origins.** Most of
what the protocol has to get right is invisible on a single-origin page: frame
inclusion policy, out-of-process iframes, cross-origin redirect visibility,
popup ownership and the exfiltration sink all need a second and a third site the
browser genuinely treats as different. `support/fixture_origin_map.h` starts one
embedded test server and gives each corpus origin its own virtual host on it, so
each gets its own security origin, its own site, its own process under site
isolation and its own cookie jar. One server and one port is the corpus's own
hosts mode, and it is required rather than convenient: a corpus page builds a
sibling origin's URL as "same port, sibling hostname", because a page cannot
learn a port it was not loaded from. Four servers on four ephemeral ports would
send every client-side cross-origin reference in the corpus to the wrong origin,
where it would 404 — and a cross-origin case that quietly commits on the
opener's origin is a same-origin case wearing the wrong name.

**The hostnames are the map's own, not the corpus's.** The corpus's four names
are subdomains of one registrable domain, and site isolation keys on the
registrable domain rather than on the origin, so serving them gives four origins
that are one site sharing one renderer process — which every assertion decided
on origin passes straight through. The map assigns one name per corpus role on
its own registrable domain instead, under both schemes, and checks the sites it
computed rather than trusting its own table. The corpus's shared cross-origin
helper carries the corpus's hostnames, so the map binds that table to what it
actually serves as the helper is served; the corpus itself is a versioned
contract and a browser test does not get to rewrite it.

**The adversarial suite needs a renderer that lies.** The real endpoint cannot
be made to: it is the code being defended, and adding a way to make it lie would
mean shipping one. `support/scripted_renderer_endpoint.h` is bound in its place
through the same associated interface the broker would have used.

## 2. What is here

| Subdirectory | Owns |
|---|---|
| `corpus/` | The single reader of the deterministic and hostile web fixture corpus, for both process halves |
| `support/` | The fixture base classes, the four-origin server, the scripted client in the core service's seat, the scripted renderer endpoint, deterministic time and identifiers, and the assertions over the journal and the audit stream |
| `parity/` | WP-M1-06: every milestone-M1 Required parity row that is only observable in a real browser |
| `correctness/` | The milestone-M2 page-intelligence suite over the golden and hostile fixtures |
| `adversarial/` | WP-M2-08: injection, hidden content, malicious renderer payloads, message bounds, races, stale handles, and sensitive-zone probes |
| `tool/` | The tool seam with its processes actually running: a real sandboxed worker over a browser-opened descriptor, the concurrency bound, and what a finished, refused or unparseable job does to a process. Everything below the seam is provable without one; none of these are |
| `benchmark/` | The Content Shell observation half of the Task Benchmark adapter in `taffy_browsertests`: one gtest case per scenario id, each emitting one `taffy-benchmark-adapter-v1` record for `tools/benchmark.d/launcher.py` to recover. It proves the transport and observes no page |
| `fuzz/` | A fuzzer for every parser and every browser-facing renderer payload, with a seed corpus generated from the contract |
| `recovery/` | A separate embedder-owned Chrome Profile test component. Its common sources run in `browser_tests` and `taffy_profile_browsertests`; its task, workspace, pause/resume, cancellation, and Task Benchmark sources are Android-only. Patches 0030 and 0045 register the component without making the Content Shell harness depend on Chrome or the stock Android suite own Taffy data |
| `data/` | A mount point. Nothing is committed there; see [data/README.md](data/README.md) |

The Android-only recovery files are intentionally explicit. The selected-page
task and saved-workspace/restart cases live in
`recovery/core_service_task_vertical_browsertest.cc`, with workspace assertions
and export helpers in `recovery/workspace_vertical_test_support.{cc,h}`. The
task-control cases live separately in
`recovery/core_service_task_control_vertical_browsertest.cc`. One cancels on
the first published task and checks the Core API and browser terminal binding.
The other pauses at a real pending action, proves the old authority cannot be
spent while paused, and requires separate durable Pausing and Paused
revisions. It terminates only the sandboxed utility process while the
browser-owned tab and source stay live, restores the same inert task and
revision in the next generation, resumes through the durable control ledger,
and completes only after approving the newly minted action.
`recovery/core_api_status_reader.{cc,h}`,
`recovery/core_api_status_reader_task.cc`,
`recovery/core_api_status_reader_workspace.cc`,
`recovery/core_api_status_observer.{cc,h}`, and
`recovery/empty_vault_profile_platform_adapter.{cc,h}` are their
generated-status and platform-boundary support. The sibling
`recovery/task_benchmark_vertical_browsertest.cc` is only the Task Benchmark's
task-side transport adapter: it starts no task and its record is rejected by
the scorer.

## 3. Three rules every Content Shell test here follows

**A missing prerequisite is a failure, never a skip.** A missing corpus, an
unmounted contract, an unbound renderer endpoint — each fails with the exact
path or the exact instruction. A suite that passed because it could not find the
pages or the seeded secrets it was supposed to read would appear in a milestone
exit-evidence packet as a green row, which is worse than a red one.

**The corpus is the authority, never a copy.** Expectations are read from
`test-fixtures/web/manifest.json` at run time. A test with a canary token or a
navigation transition typed into it would keep passing after the corpus changed
its mind, and the corpus is versioned and immutable precisely so that such a
change is deliberate and reviewed.

**Every test carries three assertions it did not ask for.** No request reached
the corpus's collection endpoint; exactly one terminal result was delivered per
request; no seeded secret reached the projection, the journal or the audit
stream. `support/taffy_browser_test_base.cc` runs all three in teardown. A test
that needs one of them to fail says so by inverting it, never by switching it
off.

## 4. Verification list for the Chromium track

Ordered by cost of being wrong, not by difficulty. Items 1 to 4 were the
build-graph work that had to happen before the Content Shell suites could run.
Items 2 to 4 have happened — the dep line is live, the portable suites reach
`taffy_browsertests`, and the data mounts exist — while item 1 is still open.
The Profile recovery component takes the separate patch 0030 and 0045 path
described above. Items 5 to 8 can now be attempted; no result from them is recorded here.

The Content Shell suite itself has one full run on record, in
TEST-INDEX.md section 2.1: 2026-08-20 on the phone, 219 tests,
195 passed, 24 failed, of which 23 are a stable baseline and one is a timeout
flake that passes under a filter. It predates the 2026-08-22 cut to `//taffy`,
so it settles none of the items below and says nothing about the separately
registered Profile recovery component. Keep it: the whole reason it was
recorded is that a count of failures is only useful next to the count it is
being compared against.

1. **Retire the two duplicate corpus readers.**
   `//taffy/browser/test/bip_fixture_manifest.*` and
   `//taffy/renderer/test/fixture_corpus.*` are superseded by
   `corpus/corpus_manifest.h`, which carries the union of what both answered.
   Retiring them is a `BUILD.gn` edit plus a delete in each of those
   directories, and it belongs to their owners rather than to this one. Until
   it happens there are three readers of one manifest, which is one more than
   before this directory existed.
2. **Add this directory to the umbrella test group.** Done for the suites:
   the dep line in `//taffy/BUILD.gn` is live, and
   TEST-INDEX.md records what landing it cost. The fuzzers
   are still reached by no binary — that half stays open (item 8, and the
   run-location table in TEST-INDEX.md).
3. **Decide how the suites reach their test binaries.** Decided and done for
   the portable groups: a standalone `test("taffy_browsertests")`, registered
   as an Android test target by patch
   [0021](../../chromium/patches/0021-register-taffy-browsertests-suite.md)
   and run via `./tools/chromium/test`. The Profile recovery component has the
   other truthful answer: patch
   [0030](../../chromium/patches/0030-register-core-recovery-browsertest.md)
   adds its test-only source set to Chrome's host binary, and patch
   [0045](../../chromium/patches/0045-register-taffy-profile-browsertests.md)
   declares the dedicated Android `taffy_profile_browsertests` binary. The
   stock `android_browsertests` target stays free of Taffy sources and runtime
   data.
4. **Mount the corpus and the contract.** Done: `./tools/chromium/sync`
   creates the three mounts `data/README.md` names, and `sync --verify`
   checks them. A suite still fails loudly at `CorpusMount` when they are
   missing, which is the designed behaviour rather than a defect.
5. **Bring up the renderer endpoint, or accept where the suites run.** The
   correctness suite refuses to run without one and its failure message names
   the three upstream files to read. The parity and adversarial suites need
   none.
6. **Run the parity suite first.** It has the fewest prerequisites, it covers
   the milestone whose exit review comes first, and a failure in it is almost
   always a real product defect rather than a harness problem.
7. **Run the adversarial suite before the correctness suite.** Its refusals are
   the ones that are silent when they break: an action on the wrong document
   does not crash, it clicks something.
8. **A browser-test wildcard must become exact names before Android
   sharding.** Chromium's generated Android wrappers use the
   `--fast-local-dev` path, which implies `--extract-test-list-from-filter`.
   Patch
   [0021](../../chromium/patches/0021-register-taffy-browsertests-suite.md)
   makes a wildcard in any registered browser-test suite take the
   device-enumeration path, and registers `taffy_browsertests`; stock
   `android_browsertests` was already on that list upstream, and patch
   [0045](../../chromium/patches/0045-register-taffy-profile-browsertests.md)
   adds `taffy_profile_browsertests` to it. So
   `Suite.*` resolves to exact names and the existing one-test shard limit
   gives each name a fresh process. Exact names, including colon-separated
   lists, still skip that roughly three-second query and remain useful for a
   focused run:

   ```bash
   out/dev-arm64/bin/run_taffy_browsertests \
     --gtest_filter=MediaToolWorkerBrowserTest.AProbeRunsInItsOwnProcess:MediaToolWorkerBrowserTest.EightJobsRunAtOnce
   ```

9. **Generate the fuzzer seed corpus and check it is not empty.**
   `fuzz/write_wire_seeds` fails rather than skipping, but confirm the output
   directory has files in it: a fuzzer with an empty seed corpus starts from
   nothing, which is indistinguishable from a fuzzer that found nothing.

## 5. Items flagged VERIFY in this directory

Grep for `VERIFY AT` for the current list. The summary at the time of writing:

| Where | What to check | If the guess is wrong |
|---|---|---|
| `corpus/corpus_mount.cc` | `base::DIR_SRC_TEST_DATA_ROOT` versus the older source-root key | One line |
| `support/fixture_origin_map.cc` | Which hostnames the embedded test server's test certificate covers | The one host table, which both schemes share; a name outside the certificate's coverage would have to move to a registrable domain inside it |
| `support/fixture_dynamic_endpoints.cc` | That request handlers run in registration order and fall through on a null return | The directory serving moves into the dispatcher |
| `support/deterministic_clock.cc` | `base::ScopedMockClockOverride` overriding all three clocks | One constructor |
| `support/canary_leak_scanner.cc` | `base::Base64Encode`'s string overload | One call |
| `support/scripted_renderer_endpoint.cc` | `OverrideBinderForTesting` on the associated-interface provider | The one install call site |
| `support/renderer_endpoint_requirement.cc` | How a downstream shell-derived binary installs its own renderer client | The failure message already names the three files |
| `parity/error_and_interstitial_browsertest.cc` | The mismatched-certificate server configuration | One enumerator |
| `parity/download_lifecycle_browsertest.cc` | Whether content_shell has a usable download delegate | The file moves to the instrumentation suite rather than being weakened into a fake |
| `fuzz/BUILD.gn` | Whether a depfile naming paths outside the source root is honoured | The third generator in this component carrying the same item |

## 6. What this directory deliberately does not do

- **It does not test a model.** There is no model in a browser test and there
  must not be. The property the adversarial suite proves is that a page's
  instruction never becomes authority, which has to hold whatever a model does.
  The model-facing half belongs to the context firewall, one layer up.
- **The portable harness does not test the sandboxed core service.** The
  scripted client plays the service-facing browser seat so that a service
  defect and a broker defect do not produce the same red. The isolated
  `recovery/` component is deliberately outside this Content Shell build graph;
  Chrome's `browser_tests` and `taffy_profile_browsertests` load it through
  patches 0030 and 0045 to test the real Profile and production utility
  registry. The
  common crash, Profile-factory, and page-host cases enter both runners. Only
  `taffy_profile_browsertests` receives the Core API task, workspace,
  pause/resume, cancellation, and task-side benchmark cases.
- **One core loss may arrive through two callbacks, but it advances the
  generation once.** A `ServiceProcessHost` terminal notification has authority
  only when its exact `ServiceProcessId` and its launch-time generation still
  match the manager. A Mojo disconnect handler carries the generation of the
  pipe it watches. Once either path handles the loss, a late callback from the
  old process or pipe is stale and cannot tear down the successor generation.
- **It does not duplicate the seam tests one directory up.**
  `//taffy/browser` proves each seam against synthetic pages, one
  property per test. This directory runs the same seams over the real corpus and
  through the whole browser-process stack, which is a different question with a
  different failure mode.
- **It does not own a parity row.** Every row belongs to the parity matrix, and
  `browser/PARITY.md` plus TEST-INDEX.md are the two maps onto
  files.
- **It does not fuzz an unknown enumeration member or a malformed message
  body through the scripted endpoint.** Neither can be sent through the
  generated bindings: every protocol enumeration is closed, so Mojo's own
  validator rejects the message first. Those properties are proved by the
  compatibility fixtures in `taffy-core/contracts/bip/compat` and by the parser fuzzers in
  `fuzz/`, both of which construct the bytes directly.

## 7. Related

- TEST-INDEX.md — every suite, the requirement or invariant it
  proves, and where it runs
- `../browser/PARITY.md` — the milestone-M1 seam-to-row
  mapping this directory's parity suite completes
- Testing and delivery
  — the test layers and which host runs which
- Browser Intelligence Protocol
  — the verification strategy and the required safety properties
- Threat model — the adversaries
  the adversarial suite is written against
- Benchmark corpus — the
  authority for the fixture list and the versioning rule
