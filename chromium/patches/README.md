# Upstream patch queue

**Status:** `[Decided]` mechanism (decisions
0001,
0012)
**Implementation status:** `[Current]` every entry that is not retired is
exported and applied against the pinned milestone. **This
document states no total.** Run `./tools/check fast --only chromium` for the
realised and projected figures: it counts the `.patch` files and their
modified upstream lines, then adds every specification that has not been
exported at the estimate that specification records for itself. A number
copied into prose here would be wrong by the next export, and a reader would
have no way to tell which of the two was stale. The export debt 0008 carried
from 2026-09-05 — an upstream commit on `taffy/patched` that no `.patch` file
mirrored, because unrelated live checkout edits kept `export-patches` from
taking a clean queue snapshot — was retired on 2026-09-06: those edits were
themselves committed, as entries 0045 to 0049, and the queue was rewritten
from the branch, so 0008's `.patch` sibling now exists and its
specification's "How this was exported" section records the closure and the
evidence still owed. 0005 **retired before it was written**
on 2026-08-21 — TaffyGo keeps the upstream user agent, and the
specification records why. 0009 — the Compose status island, the
queue's largest and riskiest entry — **retired** on 2026-08-22 after four
days applied, under decision 0024's own line item; its specification's
"How it retired" section is the record. Everything else is exported by
`./tools/chromium/export-patches` from commits on `taffy/patched` carrying
`Taffy-Patch:` trailers; the hand-written-export debt that five entries
carried through 2026-08-21 (0007, 0023, 0024, 0025, 0026 — queue files
whose lines existed only as uncommitted modifications one `sync` away from
being discarded) was retired that day by committing the five sets and
rewriting the queue from the branch, and each of those specifications'
"How this was exported" sections records the closure.
**Authority:** Chromium fork and build strategy §1

This directory carries the **only** downstream edits to files Chromium owns.
New TaffyGo code never appears here — it lives in
[`../../taffy-core/`](../../taffy-core/README.md) and is
symlink-mounted into the checkout, so it rebases with zero conflicts.

## Two kinds of file, one sequence

```text
chromium/patches/NNNN-short-name.md               # the specification
chromium/patches/NNNN-short-name.patch            # the edit itself
chromium/patches/security/NNNN-short-name.patch   # urgent cherry-picks
```

A specification is written first, reviewed on its own, and keeps its number
when the patch is exported. A number therefore names one logical change for
its whole life, whether it currently exists as an argument or as a diff.

Patches apply in filename order: `patches/` first, then `patches/security/`.
Reserve a number by using the next free one in your PR; renumbering an
existing patch is a rebase, not a rename.

**Why specifications exist at all.** They were written when no Chromium
checkout existed on the authoring host, so the upstream files these edits touch
could not be read: a hand-written diff against an unread file will not apply,
and a patch that does not apply costs more than the work it claims to have
done. A specification names the file, the symbol, the change, the reason the
overlay cannot host it, the rebase risk and the exact command that turns it
into a patch — everything a reviewer needs, and everything the engineer on the
Linux track needs, with nothing invented.

**That constraint is gone, and the lesson from it is not.** A checkout exists
and the upstream files are readable, so a specification is now a review
artifact rather than a necessity. SP-01 showed what the arms-length ones cost:
0002 shrinks from an estimated 35 lines to about 5, because upstream already
forwards two of the three arguments it was going to add; 0010 had no
specification at all, because the constraint that forced it is an assert inside
a template rather than anything the public contract documents. **Treat every
unwritten estimate below as a lower bound**, and expect the count to move in
both directions as each is written against real files.

**Every patch carries its own number.** `export-patches` reads a
`Taffy-Patch: NNNN` trailer from the commit message and names the file from it,
because `git format-patch` numbers by position on the branch and would hand
0001 to whichever change happens to be committed first — colliding with
whatever specification already owns that number. Use the number of the
specification the patch implements, or the next free number if it implements
none. The export refuses a commit with no trailer, and refuses two commits that
claim the same number.

## The specified queue

| # | Change | Upstream file | Needed by | State | Rebase risk |
|---|---|---|---|---|---|
| [0001](0001-register-product-targets.md) | Register the product target in the Android build graph | `//BUILD.gn` | WP-M1-01 | **exported** | Low |
| [0002](0002-downstream-branding-hooks.md) | Let the product carry a downstream flags file (revised: icon half moved to the overlay's resource shadowing) | `//chrome/android/chrome_public_apk_tmpl.gni` + `SplitCompatApplication.java` | WP-M1-01 | **exported** | Low |
| [0003](0003-pack-taffy-string-resources.md) | Pack the TaffyGo string resources into the product | `//tools/gritsettings/resource_ids.spec` | PAR-L10N-001 | **exported** | Low |
| [0004](0004-report-upstream-provenance.md) | Report the upstream revision and downstream delta (the product-logo half is still owed — it waits on a pak-packed TaffyGo mark) | `//chrome/browser/ui/webui/version/version_ui.cc` + version `BUILD.gn` + `about_version.html` | PAR-SEC-002 | **exported** | Medium |
| [0005](0005-user-agent-product-token.md) | Supply the user agent product token | `//chrome/browser/chrome_content_browser_client.cc` | WP-M1-01 | **retired** (2026-08-21, never written — the upstream user agent is kept) | Low |
| [0006](0006-bind-page-intelligence-service.md) | Link the page intelligence broker into the browser (the binder-map half is **retired** — decision 0032) | `//chrome/browser/BUILD.gn` | WP-M2-02 | **exported** | Low |
| [0007](0007-create-render-frame-observer.md) | Create the TaffyGo render frame observer | `//chrome/renderer/chrome_content_renderer_client.cc` + `DEPS` + `BUILD.gn` | WP-M2-03 | **exported** (whole, 2026-08-20) | Low |
| [0008](0008-route-downloads-and-external-intents.md) | Refuse unprivileged task-action downloads | `//chrome/browser/download/chrome_download_manager_delegate.cc` | WP-M1-02, CAP-AG-005 | **exported** | Low to medium |
| [0009](0009-mount-compose-status-island.md) | Mount the Compose status island | `//chrome/android/java/.../` browser controls | WP-M1-04 | **retired** (2026-08-22; the island's screens live on `TaffyBrowserActivity`) | High |
| [0010](0010-allow-kotlin-sources-under-taffy.md) | Allow Kotlin sources under `//taffy` | `//build/config/android/internal_rules.gni` | WP-M1-04 | **exported** | Low |
| [0011](0011-parse-kotlin-enum-class-in-compile-java.md) | Parse Kotlin `enum class` in `compile_java.py`'s type check | `//build/android/gyp/compile_java.py` | WP-M1-04 | **exported** | Low |
| [0012](0012-declare-compose-ui-geometry-dependency.md) | Declare `ui-geometry` as Chromium-consumable | `//third_party/androidx/BUILD.gn` + template | WP-M1-02 | **exported** | Low |
| [0013](0013-keep-kotlin-module-files-in-turbine-header-jars.md) | Keep `.kotlin_module` files in turbine header jars | `//build/android/gyp/turbine.py` | WP-M1-04 | **exported** | Low |
| [0015](0015-apply-feature-posture-at-field-trial-setup.md) | Apply the TaffyGo feature posture at field-trial setup | `//chrome/browser/chrome_browser_field_trials.cc` | decision 0019 | **exported** | Low |
| [0016](0016-override-google-bound-pref-defaults.md) | Override Google-bound pref defaults | `//chrome/browser/profiles/pref_service_builder_utils.cc` | decision 0019 | **exported** | Low |
| [0017](0017-guard-omnibox-vector-icon-fallback-for-vr-less-android.md) | Guard omnibox vector-icon callers for VR-less Android | `//chrome/browser/ui/omnibox` + searchbox WebUI | decision 0019 | **exported** | Low |
| [0018](0018-let-taffygo-product-target-reach-cwebp.md) | Let the product target reach cwebp | `//third_party/libwebp/visibility.gni` | WP-M1-01 | **exported** | Low |
| [0019](0019-build-no-popular-sites-source.md) | Build no popular-sites source for the NTP tiles | `//chrome/browser/ntp_tiles/chrome_popular_sites_factory.cc` | decision 0019 | **exported** | Low |
| [0020](0020-no-listaccounts-when-signin-disallowed.md) | No /ListAccounts when signin is disallowed | `//components/signin/internal/identity_manager/gaia_cookie_manager_service.cc` | decision 0019 | **exported** | Low |
| [0021](0021-register-taffy-browsertests-suite.md) | Give every selected Android browser test a fresh process | `//build/android/pylib/{gtest,local/device}` | WP-M1-02, WP-M2-08 | **exported** | Low to medium |
| [0022](0022-taffygo-launcher-activity.md) | Point the launcher at the TaffyGo activity | `//chrome/android/java/AndroidManifest.xml` | WP-M1-04, decision 0024 | **exported** | Low |
| [0023](0023-attach-page-intelligence-to-tabs.md) | Attach page intelligence to a tab's WebContents | `//chrome/browser/ui/tab_helpers.cc` | WP-M2-02 | **exported** | Low |
| [0024](0024-suppress-newapi-for-contentviewrenderview.md) | Suppress `NewApi` for `ContentViewRenderView` at minSdk 29 | `//chrome/android/expectations/lint-suppressions.xml` | decision 0024 | **exported** | Low |
| [0025](0025-taffygo-owns-inbound-intents.md) | Point the inbound-intent alias at TaffyGo | `//chrome/android/java/AndroidManifest.xml` | WP-M1-04, decision 0024 | **exported** | Low |
| [0026](0026-report-dom-mutations-to-the-embedder.md) | Report DOM mutations to the embedder (`WebDomMutationObserver`) | `//third_party/blink/public/BUILD.gn` + `//third_party/blink/renderer/core/exported/build.gni`, plus three new downstream-owned files | WP-M2-05 | **exported** | Low |
| [0028](0028-register-profile-core-utility-service.md) | Register the profile core utility service | `//chrome/utility/services.cc` | decision 0037 | **exported** | Low |
| [0029](0029-register-taffy-core-sql-metric.md) | Register the Taffy core SQL metric | `//tools/metrics/histograms/metadata/sql/histograms.xml` | decision 0037 | **exported** | Low |
| [0030](0030-register-core-recovery-browsertest.md) | Add the Profile recovery source set to two existing browser-test dependency lists — two added lines, and no target declaration; patch 0045 moves the Android edge onto a binary of its own | `//chrome/test/BUILD.gn` | decision 0037 | **exported** | Low |
| [0031](0031-register-taffy-java-with-libchrome.md) | Register the TaffyGo product Java closure with libchrome's final JNI generator | `//chrome/android/BUILD.gn` | Android product assembly | **exported** | Low |
| [0032](0032-register-taffy-profile-keyed-service.md) | Register the TaffyGo core-service manager before Chromium closes the profile-factory graph | `//chrome/browser/profiles/BUILD.gn` + `chrome_browser_main_extra_parts_profiles.cc` | decision 0037 | **exported** | Low |
| [0033](0033-append-filtering-url-loader-throttles.md) | Filter browser-initiated profile loads | `//chrome/browser/chrome_content_browser_client.cc` | CAP-BR-022, decision 0076 | **exported** | Low |
| [0034](0034-taffy-theme-reaches-web-contents.md) | TaffyGo theme and Dark sites reach WebContents | `WebContentsThemeClient.java` | SCR-407 | **exported** | Low |
| [0035](0035-create-cosmetic-filter-render-frame-observer.md) | Attach Ads and trackers to renderer frames and resource loaders | `//chrome/renderer` embedder seams + `DEPS` + `BUILD.gn` | CAP-BR-022, decision 0086 | **exported** | Low to medium |
| [0036](0036-bind-cosmetic-filter-host.md) | Bind the browser implementations of renderer filtering | `//chrome/browser/chrome_content_browser_client_receiver_bindings.cc` | CAP-BR-022, decision 0086 | **exported** | Low |
| [0037](0037-register-provider-auth-redirect-throttle.md) | Register the provider sign-in redirect throttle | `//chrome/browser/chrome_content_browser_client.cc` | decision 0095 | **exported** | Low |
| [0038](0038-enforce-exact-task-navigation-before-network.md) | Enforce exact task navigation before the network | `//chrome/browser/chrome_content_browser_client.cc` + `chrome_navigation_ui_data.*` | CAP-PI-009, CAP-AG-005 | **exported** | Low to medium |
| [0039](0039-persist-only-open-document-grants.md) | Persist only returned Open Document URI modes | `//ui/android/.../SelectFileDialog.java` + Robolectric regression tests | CAP-BR-010, PAR-FILE-004–006 | **exported** | Low |
| [0040](0040-let-tab-embedders-resolve-owning-activity.md) | Let a tab embedder resolve its owning Activity | `//chrome/android/java/.../InterceptNavigationDelegateClientImpl.java` + Robolectric regression | PAR-NAV-003, PAR-WEB-011 | **exported** | Low |
| [0041](0041-let-embedders-supply-download-notifier.md) | Let embedders supply the process download notifier | `//chrome/android/java/.../DownloadManagerService.java` + Robolectric regression | SCR-802 | **exported** | Low |
| [0042](0042-enable-regular-profiles-on-android.md) | Enable regular browser profiles on Android | profile capability + Android lifecycle deletion/recovery | SCR-708, CAP-BR-020 | **exported** | Medium |
| [0043](0043-supply-live-profile-download-directory.md) | Supply each page host with its profile's live download directory | `//chrome/browser/ui/tab_helpers.cc` | CAP-AG-005, `browser.download.start` | **exported** | Low |
| [0044](0044-declare-compose-animation-dependencies.md) | Declare Compose animation and animation core as Chromium-consumable | `//third_party/androidx/BUILD.gn` + template | `ui.android.core.ui`, `ui.android.core.designsystem` | **exported** | Low |
| [0045](0045-register-taffy-profile-browsertests.md) | Give the Android Profile verticals their own browser-test binary, and register it as a browser-test suite | `//chrome/test/BUILD.gn` + `//build/android/pylib/{gtest,local/device}` | decision 0037, under the one-process-per-test rule patch 0021 established | **exported** | Low to medium |
| [0046](0046-give-the-renderer-the-filtering-request-filter.md) | Give the renderer the filtering request filter | `//chrome/browser/chrome_content_browser_client_receiver_bindings.cc` + `//chrome/renderer/url_loader_throttle_provider_impl.{h,cc}` | CAP-BR-022, decision 0086 | **exported** | Low |
| [0047](0047-register-taffygo-local-state-preferences.md) | Register TaffyGo's Local State preferences | `//chrome/browser/prefs/browser_prefs.cc` | decision 0122 | **exported** | Low |
| [0048](0048-witness-physical-profile-deletion.md) | Witness the physical deletion of a profile directory, with a durable custody record | `//chrome/browser/profiles/nuke_profile_directory_utils.{h,cc}` | decision 0122 | **exported** | Medium to high |
| [0049](0049-delete-a-marked-ephemeral-profile-on-android.md) | Delete a marked ephemeral profile on Android | `//chrome/browser/profiles/profile_manager.{h,cc}` + `profile_manager_unittest.cc` | decision 0122 | **exported** | Medium to high |
| [0050](0050-declare-downstream-credit-directories.md) | Declare the existing downstream credits-directory argument | `//components/resources/BUILD.gn` | decision 0127 | **exported** | Low |
| [0051](0051-let-the-overlay-drop-query-all-packages.md) | Let a downstream manifest drop the package-query permission, by wrapping `QUERY_ALL_PACKAGES` in a block an extending template can override | `//chrome/android/java/AndroidManifest.xml` | decision 0153 | **exported** | Low |

**For the running total, run the lane:**

```bash
./tools/check fast --only chromium
```

It prints four figures and the share of each bound they consume — realised
patches, realised modified upstream lines, and the same two projected — and it
refuses to blend them, because only the realised pair can block a build. Every
per-change figure it sums is read out of this directory at run time: the
`.patch` files for the realised half, and each unexported specification's own
`**Estimated size:**` header line for the projected half. That is why the
table above carries a state rather than a number. `tools/lib/fork_debt.sh` is
the module that does the counting and `tools/README.md` describes it.

Two specifications are retired, and nothing now awaits export.
0005 retired
on 2026-08-21, before it was written: the seams SP-01 confirmed made the
patch feasible, and the compatibility and fingerprinting-consistency
reasoning in the specification made it unwanted — TaffyGo keeps the upstream
user agent and identifies itself on its own surfaces instead. 0009 retired
on 2026-08-22, after being applied and proven: decision 0024 gives TaffyGo
every screen, the launcher had pointed at `TaffyBrowserActivity` since patch
0022, and the island the patch mounted in `ChromeTabbedActivity` was
unreachable dead UI — so the upstream commit was dropped and the overlay's
island retired with it. Decisions 0089 and 0090 settled 0008's enforcement
semantics, commit `faa85b0949559` implements its one upstream call site, and
the 2026-09-06 export finally put that commit in the queue as a `.patch`
file; OD-056 remains open for the penetration-test and corpus evidence, not
for the download decision.

Two of the written patches grew a second half their specification never
mentioned: 0006 and 0007 each specify a `.cc` call-site edit but not the
`deps +=` in `//chrome/browser/BUILD.gn` and `//chrome/renderer/BUILD.gn`
that makes the symbol link. Without those edges the call sites do not
compile, and with them TaffyGo's C++ enters libchrome — which is also how it
reaches the APK, since `assert_no_native_deps` forbids the Java side from
carrying it.

Those two have since diverged, and the divergence is the more useful lesson.
**0006's `.cc` half is retired**: the interface it wanted to register in the
per-frame binder map is channel-associated on both ends, so it never travels
through that map at all, and the `deps` edge is the whole of the change
(decision
0032,
which closes OD-027). **0007's `.cc` half is real** and is written into the
checkout awaiting a `--fixup` onto the commit that carries the edge. The
production wiring 0006 was reaching for turned out to live in a third file
entirely, which is patch 0023.

## Rules

1. **One logical change per patch.** A patch that touches three unrelated
   upstream areas is three patches.
2. **Every patch header names an owner and a reason** in its commit message,
   plus how it is retired: upstreamed, refactored into `//taffy`,
   or "permanent, reviewed at each milestone rebase". The specification's
   "Retirement" line is where that sentence comes from.
3. **Never hand-edit a `.patch` file.** The editing surface is the
   `taffy/patched` branch in the checkout; regenerate the queue with
   `./tools/chromium/export-patches`, which fails if the branch and the queue
   have diverged.
4. **Prefer the overlay.** If an upstream edit can be replaced by a hook, a
   delegate, or a new file under `//taffy`, do that instead — the
   patch queue is the expensive option, paid at every rebase. Every
   specification above has a "why the overlay cannot host it" section because
   that question is asked first, every time.
5. **A specification is not a patch.** Exporting the patch is what makes the
   change real; until then the change does not exist and nothing may describe
   it as applied.
6. **Exporting a patch changes its specification's status label, in the same
   change.** A specification is `[Proposed]` while the edit may not be needed
   at all, `[Decided]` once it is agreed but no diff exists, and `[Current]`
   from the moment its `.patch` sibling is exported — because from then on the
   specification describes something a build applies rather than something
   somebody intends. A `[Decided]` specification with an exported sibling is a
   defect, and the cheapest kind to leave behind: the state column above and
   `./tools/check fast --only chromium` both count it as realised while its
   own header says it is not.
7. **When the exported patch differs from what its specification described,
   the specification records the difference rather than being quietly
   rewritten.** The specification is the argument that was reviewed, and a
   reader needs to know that upstream did not turn out to be the shape the
   argument assumed. Add the correction where the wrong claim was, name what
   is actually in the `.patch`, and say what it cost — that is how the
   estimate discipline above learns anything.

8. **Every patch carries the project identity, not the person who exported
   it.** The `From:` line of all 47 files reads
   `Matterward Labs <admin@matterwardlabs.com>`, matching the copyright holder
   in [`NOTICE`](../../NOTICE), and
   `./tools/check fast --only chromium` refuses any patch that does not.

   It is normalised by `./tools/chromium/export-patches` rather than by hand,
   for a reason worth stating because it is not obvious. `git format-patch`
   writes the commit author, `git am` reads that line back when the queue is
   applied, and the checkout's commits are therefore whatever identity the
   last sync installed. Editing the files alone would work until somebody
   exported from a checkout that had not been re-synced, at which point the
   queue would silently take on their personal address again — nothing checks
   a `From:` line for plausibility, and a wrong one is a valid one. The
   identity is one constant, `TAFFY_PATCH_AUTHOR` in
   `tools/chromium/lib/chromium.sh`, read by both the exporter and the gate.
   Changing it is a one-line edit there followed by a re-export.

   This is rule 3 seen from the other side: you still never hand-edit a
   `.patch`, and the exporter is still the only writer.

## Fork-debt budget

Three bounds, set by decision
0013 and
chromium fork and build
§1.3, which is their authority. The first two are executable and the third is
not.

| Bound | Where the current figure comes from |
|---|---|
| Patches in the queue | `./tools/check fast --only chromium`, realised and projected |
| Modified upstream lines, total | the same command, realised and projected |
| Milestone rebase within an engineer-time bound | chromium fork and build item 17, the rehearsal that measured it — **textual resolution only**, since no build ran at the rehearsed milestone |

Neither the limits nor the current figures are written down here. A limit that
appears in two documents eventually appears with two values, and a count copied
into prose is stale the next time somebody exports a patch. The third bound is
not executable for the same reason it was unmeasured for so long: only a rebase
produces it, so its figure lives in the evidence record of the run that
produced it and is replaced by the next one. That record also states what the
figure excludes — the queue was replayed textually, and compile, link and test
cost at a new milestone is still unmeasured.

Exceeding either executable bound blocks new upstream-file edits until the
delta is refactored into `//taffy` or upstreamed. A figure inside
80% of a bound passes; above that the lane warns, because the last moment at
which shedding debt is cheap is before the bound is reached. Nothing automates
any of this: whoever changes the queue runs the command (decision
0023).

## Round trip

```bash
./tools/chromium/sync              # applies this queue onto the taffy/patched branch
# ...edit upstream files in the checkout, commit on taffy/patched...
./tools/chromium/export-patches    # regenerates this directory from those commits
```

`./tools/chromium/import-patches` re-applies the queue onto an already-synced
checkout without a full `gclient sync` — use it after pulling a PR that
changes the queue.
