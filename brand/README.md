# TaffyGo brand assets

## Taffy character

`character/taffy-character-master.webp` is the approved visual reference for
Taffy. It establishes an anime-fantasy appearance with swept dark hair, a
cyan-to-coral highlight, pointed ears, paired energy wings, an ivory and navy
fantasy-tech outfit, and a floating browser card.

The master is a 1024 x 1536 WebP with an alpha channel. Keep the character's
face, hair, palette, wing language, outfit, and proportions consistent when
making new poses. The Android UI does not ship a packaged character pose.

The website uses two generated, visually reviewed poses derived from this
master: the hero pose and the bookmark pose under
`website/public/characters/`. Their prompt intent, dimensions, byte counts and
checksums are recorded in
`website/taffy-generated-assets.txt`. They are website
artwork only and do not replace the approved character master.

## Logo assets

This directory contains the six transparent logo treatments shown in the
original `taffy_spcs_logo.png` concept sheet.

`preview.png` shows all six assets on their intended light or dark surfaces;
it is a reference sheet, not a transparent production asset.

## Production PNGs

All files in `png/` are lossless 8-bit RGBA PNGs with genuine transparency.
Standalone marks use a 4096 x 4096 canvas; horizontal lockups use a
4096 x 2048 canvas.

| Asset | Intended surface |
|---|---|
| `png/taffygo-mark-color-on-dark.png` | Dark backgrounds |
| `png/taffygo-mark-color-on-light.png` | Light backgrounds |
| `png/taffygo-mark-monochrome-on-dark.png` | Dark backgrounds |
| `png/taffygo-lockup-color-on-dark.png` | Dark backgrounds |
| `png/taffygo-lockup-color-on-light.png` | Light backgrounds |
| `png/taffygo-lockup-monochrome-on-dark.png` | Dark backgrounds |

`on-dark` and `on-light` describe the background on which an otherwise
transparent asset is intended to appear.

The source artwork spells the wordmark as `taffyGo`; that casing is preserved
verbatim in every lockup. This differs from the product name `TaffyGo` used in
repository prose.

## Website social preview

The current 1200 x 630 preview is
`website/public/og-image-2655c535d406.jpg`: a generated painting with the
approved colour-on-light lockup composited over it, never generated or redrawn.
Its provenance is recorded in `website/taffy-generated-assets.txt`.

## Source and native extractions

- `source/taffy-spcs-logo-sheet.png` is an untouched copy of the supplied
  3-by-2 concept sheet.
- `source/extractions/` contains the native-resolution transparent outputs
  used to build the 4096 px production masters.

## Extraction prompt set

The built-in image editor was used once per source treatment with this shared
specification:

> Extract only the requested logo treatment from the supplied source sheet.
> Preserve its exact silhouette, ribbon widths, curves, fold and overlap
> order, gradients or silver treatment, highlights, proportions, spacing, and
> edge character. For lockups, preserve the exact text `taffyGo`, casing,
> weights, kerning, alignment, and bold `Go`. Center the result with balanced
> clean padding on a genuinely transparent RGBA canvas. Remove the rounded
> tile and every background, glow, shadow, halo, border, watermark, and other
> sheet content. Do not redesign, recolor, simplify, sharpen, or restyle it.

The six requested treatments were the top-left, top-middle, and top-right
standalone marks followed by the bottom-left, bottom-middle, and bottom-right
horizontal lockups. Because the editor's first transparency pass rendered a
checkerboard as pixels, each treatment received a corrective pass on a flat
`#00FF00` matte before deterministic keying and edge decontamination to a
real alpha channel.
