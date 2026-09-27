# 0009 — Mount the Compose status island in the Android UI

**Status:** **retired** (2026-08-22) — applied 2026-08-18, removed by decision
0024's own line
item: removing this patch is the one budget line 0024 gives back. The upstream
commit was dropped from `taffy/patched` and the `.patch` file removed by
`./tools/chromium/export-patches`; the overlay's `island/` and `upstream/`
sources, their tests and their string resources went in the same change. This
specification stays as the historical record of what was mounted, why, and
what it proved. While it was applied, the island rendered and its kernel
connection answered on a physical device the same night it landed.
**Needed by (historically):** WP-M1-04 — one bounded Compose island with
lifecycle, state restoration, accessibility and rebase coverage
**Estimated size:** ~30 modified upstream lines, 4 files (as applied; now 0 —
the patch is retired)

## How it retired

Decision 0024
supersedes 0003 and pre-authorised exactly this removal: mechanism B — a
Compose island at an upstream seam — has no remaining user once SCR-101 lands
on `TaffyBrowserActivity`. The launcher has pointed at `TaffyBrowserActivity`
since patch [0022](0022-taffygo-launcher-activity.md), so the island in
`ChromeTabbedActivity` was unreachable dead UI — a launcher tap reached
neither the strip nor its "Check again" control — and retiring this patch pays
back the queue's largest, riskiest entry.

Nothing about core-service startup depended on the island. The shipping shell
opens the profile Core API from `TaffyBrowserActivity`, while Rust runs in the
profile utility service and the retained Compose screens consume the generated
browser facade.

The `upstream/` rebase-alarm subsystem retired with the island. Its own
membership rule — an upstream symbol belongs in the declaration when nothing
in the build would notice it disappearing, i.e. silently-resolved runtime
couplings only — left it empty once the island's reflective attachments went:
the shell's upstream couplings are all compile-visible, which is decision
0024's "rebase cost moves in the cheap direction: compile breaks, not merge
conflicts". Compile visibility is now the rebase signal. If a reflective
upstream coupling ever reappears, the alarm pattern returns with it.

What was never produced: the per-rebase cost figure the Rebase risk section
below was written to measure. The island retired before any milestone rebase
happened, so that first-hand number does not exist and now never will;
decision 0024 made the retirement unconditional rather than waiting for it.

## Upstream files and symbols

As applied at the pinned milestone — the layout names this specification
guessed at were resolved against upstream source, and there turned out to be
two layout variants behind a switcher, both of which must carry the
container:

| | |
|---|---|
| File | `//chrome/android/java/res_app/layout/main.xml` |
| Symbol | `taffy_island_container`, added beside `bottom_container` |
| File | `//chrome/android/java/res_app/layout/main_forked_with_secondary_ui_container.xml` |
| Symbol | the same container — `MainLayoutSwitcher` picks between the two variants and their `LINT.IfChange` pair binds them |
| File | `//chrome/android/java/src/org/chromium/chrome/browser/ChromeTabbedActivity.java` |
| Symbol | attach in `finishNativeInitialization()`, destroy first in `onDestroyInternal()` |
| File | `//chrome/android/BUILD.gn` |
| Symbol | `chrome_java` deps gains `//taffy/app/android:island_java` |

## The change

Add one view to the layout and one coordinator construction beside the
existing ones:

```java
mTaffyStatusIslandCoordinator =
    new TaffyStatusIslandCoordinator(mTaffyIslandContainer, getLifecycle());
```

Everything else — the `ComposeView`, the composition strategy, the state
holder, the accessibility semantics and the destruction — is in
`//taffy/app/android/java/src/org/chromium/taffy/island/`
and is unit tested there without a device.

## Why the overlay cannot host it

A view has to be attached to a view hierarchy that upstream owns, and a
coordinator has to be created and destroyed on the activity lifecycle that
upstream drives. There is no downstream extension point in the Chrome Android
UI: no plugin surface, no coordinator registry, no layout include reserved for
an embedder.

Decision 0003 chose an *island* precisely because of this. One attachment
point is the smallest upstream edit that still proves lifecycle, state
restoration, accessibility and rebase cost; every additional surface would be
another attachment point and another patch.

## Rebase risk

**High — and measuring it is the point of the patch.** The Chrome Android UI
is the most actively refactored area this queue touches, and a layout or
coordinator rename conflicts directly. That is not a reason to avoid the
patch; it is the number the widening of Compose adoption was made conditional
on. Record the conflict count and the engineer-time this patch
costs at the first milestone rebase after it lands, in the rebase report.

Two things keep the cost bounded:

- the island renders one strip and owns no browser state, so a conflict is
  about *where* it attaches, never about what it shows;
- it says nothing about security, so it can never be mistaken for browser
  security UI and can be detached entirely without changing a safety property.

**Retirement:** the island is removed and the same information moves to a
surface TaffyGo owns outright. The superseded decision 0003 set that as a
conditional — if the measured rebase cost proved unacceptable — and decision
0024 makes it
unconditional, because a TaffyGo-owned activity has nowhere to attach an island
inside Chrome's layout. The measurement this patch was written to produce is
still worth taking before the removal: it is the only first-hand number the
repository will ever have for what attaching to Chrome's UI costs per rebase,
and 0024's whole argument is that the number is high.

## Verify at SP-02

1. That Kotlin compiles for a `//components` target at the pin — the Kotlin
   source allowlist is a `TOOLCHAIN.md` row and
   `//taffy/app/android:java` has to be on it. Nothing else here
   matters if it is not.
2. Which androidx Compose targets exist in `//third_party/androidx`. The
   island needs compose-runtime, compose-ui, compose-material3,
   lifecycle-runtime-compose and kotlinx-coroutines. Anything missing goes
   through Chromium's androidx roll process, which is the largest unknown in
   SP-02.
3. The APK and bundle size delta from linking Compose at all, measured before
   a second island exists.
4. The insets, IME, selection, TalkBack traversal order, tab switching,
   fullscreen video, permission prompt, multi-window, activity recreation,
   renderer death and cold start list in system architecture section 7.2,
   which is `[Open (OD-025)]`.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the layout and the coordinator in the checkout; commit as one change
./tools/chromium/export-patches
./tools/check fast
```
