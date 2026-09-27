# UI host icon derivation

**Status:** `[Current]` — the generator, the manifest and the forty committed
files exist and are verified by `./tools/check fast --only icons`. The artwork
they carry is a stand-in: the app icon and the launcher background colour are
decided by
record 0025 §7
and the layers have not been redrawn against that decision yet.

[`generate_launcher_icons.py`](generate_launcher_icons.py) derives every raster
icon the UI host ships from the committed marks in
[`brand/png/`](../../../../../brand/README.md).
[`manifest.json`](manifest.json) is the single source of what exists: which
sets, from which master, at which density, at which pixel size. Every derived
file's digest is in [`checksums.sha256`](checksums.sha256), which is a plain
`sha256sum` file — `sha256sum -c` reads it from the repository root without
this script.

## What is derived

Eight sets, five densities each.

| Set | Master | Output |
|---|---|---|
| Launcher foreground | `taffygo-mark-color-on-dark.png` | `app/src/main/res/mipmap-*/ic_launcher_foreground.webp` |
| Launcher themed layer | `taffygo-mark-monochrome-on-dark.png` | `app/src/main/res/mipmap-*/ic_launcher_monochrome.webp` |
| Splash icon, light system | `taffygo-mark-color-on-light.png` | `app/src/main/res/drawable-*/taffy_splash_icon.webp` |
| Splash icon, dark system | `taffygo-mark-color-on-dark.png` | `app/src/main/res/drawable-night-*/taffy_splash_icon.webp` |
| In-app mark, dark theme | `taffygo-mark-color-on-dark.png` | `core/ui/src/main/res/drawable-*/taffy_mark_on_dark.webp` |
| In-app mark, light theme | `taffygo-mark-color-on-light.png` | `core/ui/src/main/res/drawable-*/taffy_mark_on_light.webp` |
| In-app lockup, dark theme | `taffygo-lockup-color-on-dark.png` | `core/ui/src/main/res/drawable-*/taffy_lockup_on_dark.webp` |
| In-app lockup, light theme | `taffygo-lockup-color-on-light.png` | `core/ui/src/main/res/drawable-*/taffy_lockup_on_light.webp` |

Three jobs, three padding rules, which is the whole reason the sets are separate
files rather than one asset used three ways.

The two launcher sets fill an adaptive-icon layer: a 108 dp square whose middle
72 dp is the only part a launcher promises to show. The mark spans the
manifest's `mark_dp` of that square and is centred, so it stays inside the safe
zone under every mask and every parallax offset.

The two splash sets fill the slot the platform reserves for a launch icon with
no icon background: a 288 dp square of which only a centred 192 dp circle
survives, everything outside it masked away. `splash_mark_dp` is not that
circle inscribed by arithmetic — the ribbon's corners are transparent, so
inscribing its bounding box would waste about a tenth of the slot. It is the
largest span at which no pixel of either master reaches the circle, measured
from the masters themselves. Which of the two a device gets is decided by the
`night` resource qualifier rather than by `TaffyTheme`, because the platform
draws that window before any of the app is running to be asked; the same
reason the splash window's colour is a `values-night` resource.

The four in-app sets are **not** launcher layers and carry no safe-zone padding.
The first-run screens draw the mark at 68 dp and at 42 dp, and a screen asset
padded like a launcher layer would render the mark at two thirds of the size the
caller asked for. `TaffyBrandMark` and `TaffyBrandLockup` in `:core:ui` draw
them; each picks the treatment from the theme in scope, so both files of a pair
are always present.

| Density | Launcher layer | Mark inside it | Splash canvas | Mark inside it | In-app mark |
|---|---|---|---|---|---|
| mdpi | 108 px | 60 px | 288 px | 152 px | 72 px |
| hdpi | 162 px | 90 px | 432 px | 228 px | 108 px |
| xhdpi | 216 px | 120 px | 576 px | 304 px | 144 px |
| xxhdpi | 324 px | 180 px | 864 px | 456 px | 216 px |
| xxxhdpi | 432 px | 240 px | 1152 px | 608 px | 288 px |

The in-app lockups are rectangular rather than square, because the supplied
wordmark is part of that artwork; their sizes are in `manifest.json`.

## The pipeline

One pass per set, and every step is deterministic — no clock, no randomness,
nothing written into the output that a second run would not write again.

1. Decode the master with the overlay's `png_io`, imported rather than copied.
   Two icon pipelines that disagreed about how a master decodes would produce
   two different marks from one file.
2. Crop to the smallest box holding every pixel that is not fully transparent.
   The masters are 4096 px canvases with the mark floating in the middle, so
   that margin is the master's framing, not a design decision.
3. Resample, longest side first, so the mark spans exactly the size the manifest
   declares. The resampler box-averages weighted by opacity and refuses to
   enlarge, which is what keeps a dark fringe off the ribbon's edge.
4. Centre the result on a transparent square by copying its rows in. Compositing
   it instead would multiply each colour channel by its own opacity and put back
   the fringe step 3 avoided.
5. Write a temporary PNG and encode it with
   `cwebp -lossless -exact -z 9 -metadata none`. Lossless, so the committed
   bytes are the resampler's output and nothing else; `-exact` keeps the colour
   under fully transparent pixels; `-metadata none` keeps a timestamp or a
   colour profile out of a file review has to read as a digest.
6. Record the digest.

Deriving all forty files takes about seventy-five seconds of host Python, almost
all of it decoding five 4096 px masters. A master is decoded and cropped once
and reused by every set that names it, so adding a set that shares one costs
only its own resampling.

## Running it

```
python3 taffy-core/ui/android/tools/icons/generate_launcher_icons.py --generate
python3 taffy-core/ui/android/tools/icons/generate_launcher_icons.py --check
python3 taffy-core/ui/android/tools/icons/generate_launcher_icons.py --list
```

`--generate` needs `cwebp`, and refuses to run unless its version is the one
`manifest.json` pins. WebP encoders are not byte-compatible across releases, so
a second encoder would rewrite every committed file with nothing reviewable
having changed.

`--check` needs no encoder and no image library. It re-reads the masters'
digests, the committed files' digests, and the pixel dimensions each file
records in its own VP8L bitstream header — which is what catches a correctly
named copy of the wrong density. It runs wherever `python3` does, which is why
the `icons` lane of `./tools/check fast` can run on a documentation-only host.

## Why these bytes are committed

The overlay's own
[icon set](../../../../resources/branding/icons/README.md)
commits nothing and derives everything at build time from a GN action. This
directory does the opposite, and the two are not in conflict — the difference is
the build system, not the principle.

GN has a rule this generator can hang off; the Android Gradle Plugin does not.
Deriving at Gradle time would mean either a task that fails on any contributor
without a suitable Python, or an icon that silently goes stale. Committing the
files plus their digests moves that failure into a gate: `manifest.json` records
each master's digest, `checksums.sha256` records each output's, and the `icons`
lane fails if any of them stops agreeing. The bytes are reviewable in the sense
that matters — they are reproducible from one command and one master.

## What is deliberately absent

- **No `roundIcon` and no legacy per-density `ic_launcher.png` set.** `minSdk`
  is 29, so every supported launcher resolves the adaptive icon in
  `mipmap-anydpi/ic_launcher.xml` and applies its own mask. A round variant and
  a pre-masked square set would be two more copies of the same mark that no
  supported device asks for, each able to drift from the other two.
- **No vector splash icon**, which is what the platform's launch-screen guide
  asks for. The mark is a gradient ribbon and `brand/` holds no vector form of
  it, so a drawable would have to be redrawn by hand against the raster — new
  artwork, decided here, ahead of the record that owns it. The density set is
  the interim, and it is authored for the slot rather than borrowed from the
  launcher layer, so nothing is upscaled at draw time.
- **No monochrome master of its own for the themed layer.** A launcher reads
  that layer as a shape and recolours it, so the generator flattens the colour
  channels to white and lets the transparency channel carry the mark. The
  monochrome master is still the input, because its silhouette is the one drawn
  for a single-colour treatment.
- **No brand colour.** The launcher background is a flat colour in
  `app/src/main/res/values/colors.xml`, restating the dark theme's `surface`
  token. That is the colour
  record 0025
  §7 chose, so the stand-in and the decision now agree; the value still lives in
  the record and in the palette, never in this directory's prose.

## Related

- [TOOLCHAIN.md](../../../../../TOOLCHAIN.md) — the `cwebp` pin, whose machine
  owner file is this directory's `manifest.json`
- [The command suite](../../../../../tools/README.md) — the `icons` lane
- [The UI host](../../README.md) — what draws these files
