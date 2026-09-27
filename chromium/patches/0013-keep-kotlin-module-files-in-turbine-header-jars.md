# 0013 — Keep `.kotlin_module` files in turbine header jars

**Status:** `[Current]` — applied 2026-08-17 as part of the first ARM64
product build
**Needed by:** WP-M1-01 — the Compose island is Kotlin consuming Kotlin, and
no such edge compiles without it
**Estimated size:** ~4 modified upstream lines, 1 file

## Upstream file and symbol

| | |
|---|---|
| File | `//build/android/gyp/turbine.py` |
| Symbol | the `--kotlin-jar-path` merge at the end of `main` |

## The change

The turbine wrapper builds a target's header jar and then merges in the
Kotlin ABI classes, filtering the merge to `.class` entries only. That filter
also discards `META-INF/main.kotlin_module` — the module file that maps
packages to their file facades. Without it, kotlinc resolves a dependency's
Kotlin *classes* (they look like Java) but none of its *top-level
declarations*: every cross-target extension function, top-level composable or
package-level property fails with `unresolved reference`, while the facade
class sits in the jar with complete metadata. The failure was isolated
outside the build: the same probe file compiles against the pre-merge ABI
jar and fails against the merged header jar, and restoring the one
`.kotlin_module` entry to the merged jar fixes it.

The filter keeps `.kotlin_module` entries as well as `.class` entries.
Nothing else changes.

## Why the overlay cannot host it

The merge lives in Chromium's turbine wrapper, which every `android_library`
runs; there is no per-target hook around it.

Upstream has not hit this because its Kotlin allowlist is four paths and no
upstream Kotlin target consumes another's top-level declarations through a
header jar. TaffyGo does exactly that throughout the mounted product graph:
`//taffy/app/android:host_java` calls composables from
`//taffy/app/android/ui:android_ui_java`, whose feature targets in turn consume
top-level declarations from `core_designsystem_java` and `core_ui_java`.

**Upstreamable.** A plain defect fix upstream will need the moment two of its
own Kotlin targets share top-level API; it should be proposed upstream, which
is the path by which this patch retires early.

## Rebase risk

**Low.** Four lines in a slow-moving wrapper. If upstream restructures the
Kotlin interface-jar flow, the patch conflicts loudly and the rebase
re-answers whether the new flow carries module files — the right question to
be forced to ask.

**Retirement:** when upstream takes the fix or restructures the flow to carry
Kotlin module metadata itself.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit build/android/gyp/turbine.py in the checkout; commit with the
# Taffy-Patch trailer
./tools/chromium/export-patches
./tools/check fast
```
