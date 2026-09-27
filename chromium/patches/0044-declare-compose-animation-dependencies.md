# 0044 — Declare Compose animation dependencies for Chromium consumers

**Status:** `[Current]` — required by the retained Compose sheet and
transition primitives and first proved missing by the configured ARM64
bytecode dependency check
**Needed by:** `ui.android.core.ui` and `ui.android.core.designsystem` — the
retained surfaces directly reference `DurationBasedAnimationSpec`, `SnapSpec`,
`AnimatedContent`, and its enter and exit transitions
**Estimated size:** ~20 modified upstream lines, 2 files

## Upstream files and symbols

| | |
|---|---|
| File | `//third_party/androidx/build.gradle.template` |
| Symbol | the `dependencies` block's `compileLatest` list |
| File | `//third_party/androidx/BUILD.gn` |
| Symbol | the `androidx_compose_animation_animation_java` and `androidx_compose_animation_animation_core_java` public groups |

## The change

Declare both `androidx.compose.animation:animation` and
`androidx.compose.animation:animation-core` in the AndroidX generator template
and apply the generated consequence to `BUILD.gn`: remove the AndroidX-only
visibility restriction from their public Java groups. TaffyGo can then name
the groups as direct dependencies, which Chromium's bytecode validator
requires for directly referenced animation classes.

These are one logical change at one generator seam. The two groups are used by
different retained modules, but they are exposed by the same declaration,
retire on the same AndroidX regeneration, and have the same rebase failure
mode. Keeping them in separate patches made one upstream-file edit cost two
queue positions without creating an independently removable change.

The AAR targets and their dependency closures are unchanged. This patch only
exposes the already-pinned public groups to Chromium consumers.

## Why the overlay cannot host it

The visibility restrictions are inside Chromium's generated AndroidX build
file. A downstream wrapper cannot widen another target's visibility, and
depending on either private AAR target would encode the same forbidden edge
under a different label.

## Rebase risk and retirement

**Low, self-retiring at an AndroidX roll.** Once the template declarations are
consumed by the generator, it emits both public groups without the
restrictions. Until then the two-file edit reapplies mechanically. Android
owns the patch and reviews it at every milestone rebase.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the two Chromium-owned files; commit with `Taffy-Patch: 0044`
./tools/chromium/export-patches
./tools/check fast
```
