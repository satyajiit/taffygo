# Launcher icon set

**Status:** `[Decided]` derivation mechanism; the layers that need a flat
brand colour now have one to use —
record 0025
§7 — and have not been drawn against it yet
**Implementation status:** `[Current]` the manifest and the generator are
verified, and `//taffy/resources/branding:launcher_icons` is in the
`taffy_public_apk` graph; no icon is committed, by design

[`manifest.json`](manifest.json) is the single source of the density set:
which sizes exist, at which density, derived from which committed mark. Both
[`../tools/generate_icons.py`](../tools/generate_icons.py) and
[`../BUILD.gn`](../BUILD.gn) read it, so adding a density is a one-file
change and the build graph cannot disagree with the generator.

## What is generated

| Density | Size | Scale |
|---|---|---|
| mdpi | 48 px | 1x |
| hdpi | 72 px | 1.5x |
| xhdpi | 96 px | 2x |
| xxhdpi | 144 px | 3x |
| xxxhdpi | 192 px | 4x |

All five are the same 48 dp launcher icon, and all five come from
`brand/png/taffygo-mark-color-on-dark.png` — a 4096 px master with genuine
transparency. The on-dark treatment is the master for every density because a
launcher composites the icon over a surface it owns, so the treatment designed
for dark surfaces is the one whose gradient survives.

## Why nothing is committed here

An icon in this directory would be a binary blob that review cannot read and a
second source of truth for a mark that `brand/` already owns. Instead the set
is derived at build time by a GN action, from a master whose digest the
manifest records. Three consequences worth stating:

- **A brand change is a one-file change.** Replace the master, update
  `source_sha256`, rebuild. If the digest and the file disagree the generator
  stops rather than silently shipping the old artwork under the new name.
- **`--check` is meaningful.** The encoder writes filter-0 rows at a fixed
  compression level, so the same master and manifest produce the same bytes on
  every host. A mismatch means the input changed, never that the encoder
  drifted.
- **The resampler is honest about its limits.** It box-averages weighted by
  opacity — averaging colour without that weighting drags transparent black
  into the edges and leaves a dark halo around the ribbon — and it refuses to
  enlarge. Enlarging a launcher icon from a 4096 px master
  never happens, and a silent upscale would be a different, softer filter
  chosen by accident.

Generating the whole set takes on the order of twenty seconds of pure Python:
one decode of the master, one integer reduction, then one area average per
density. That is paid once per clean build of a tree that takes hours, and it
buys a build with no image library in its dependency graph.

## What is not generated, and why

The adaptive icon, the round icon and the store listing icon all need an
opaque background layer, which is a flat brand colour.
Record 0025
§7 decides that colour and is the only place it is written down; nothing here
invents one and nothing here copies one. What is still missing is the drawing:
the committed marks are transparent and so is the concept sheet's tile, so each
layer has to be generated against that background before it exists.
[`../README.md`](../README.md) carries them as numbered items with the same
reason. Until they are drawn the product ships the legacy square launcher icon,
which is complete and valid on every supported Android version.
