# `:core:designsystem`

**Status:** `[Current]` Colour, type, spacing, and the one place a hexadecimal
colour word exists.

Owning milestone: M0, work package
WP-M0-07.

Authoritative specifications:
ux-spec.md sections 11 and 12;
browser-parity-matrix.md
rows PAR-A11Y-003 and PAR-A11Y-004;
android-app-architecture.md
section 5. The token values themselves mirror the design handoff,
DESIGN.md sections 2, 3 and 5.

## The authority boundary

**This module defines tokens and the theme change itself; it draws no screen
content.** It publishes the theme, the colour roles, the type roles, and the
spacing scale. No screen names a colour, a size, or a typeface directly — a
screen names a role, and this module decides what that role looks like in light
and in dark. A theme change crosses the whole window once with a rising sun or
moon; reduced-motion mode cuts over without that transition.

The contrast rule is executable rather than aspirational. The palette lives in an
internal object as raw colour words, and the ratio between the pairs the design
actually uses is computed and asserted in a plain unit test on the host. A token
pair that fell below its threshold would fail the build rather than a review.
The handoff's quiet `outline` is the ordinary 1 dp boundary for cards, fields,
chips, and secondary controls. A selected state uses `hairline` or a semantic
tone when the state itself needs stronger contrast; vendor controls keep their
own specified boundary. Borders are not promoted globally, because that would
erase the handoff's hierarchy between available and selected controls.

## What this module deliberately does not do

It has no components. A button, a row, a chip, or a screen frame is a
`:core:ui` concern; this module only says what colour and size they are. It owns
no strings and no user-facing words. It reads no preference: which theme is in
force is a decision the shell makes and passes in. The window-width bucket is
measured from the window (or passed by a preview), and the spacing and type
roles follow it — a tablet is not a stretched phone.

## What each area owns

| Area | Owns |
|---|---|
| `TaffyTheme` | The composable, the object, the composition locals, and the Material 3 bridge |
| `TaffyThemeTransition` | The one-shot full-window sunrise or moonrise between resolved themes; skipped for reduced motion |
| `TaffyWindowWidth` | Compact, medium and expanded, at the Material 3 breakpoints |
| `TaffyEdges` | The four window edges as insets, so no surface writes the expression twice |
| `TaffyColors` | The colour roles a surface may name, the ribbon family of decision 0103 included: it is a token set with a contrast test over it, not the handoff's local constants |
| `TaffyTypography` | The semantic type roles; body/detail/caption/micro form the compact 14/13/12/11sp reading hierarchy |
| `TaffyShapes` | Soft Pulse radii (8 / 10 / 14 / 18 / 24); there are no elevation tokens — separation is borders and tonal steps |
| `TaffyBorders` | The border widths: standard, emphasis, rail |
| `TaffyBorderComet` | The travelling-light stroke a pill border may carry; callers drive the phase |
| `TaffySpacing` | The spacing scale, including the phone and tablet gutters, 14 dp card padding, and the minimum touch target |
| `TaffyStatusTone` | The tones a status may be drawn in, never its only signal |
| `PseudoLocalizer` | The transformation behind the pseudo-localized variant |
| `internal/TaffyPalette` | Compose-facing access to the generated platform-neutral palette |
| `internal/TaffyTypeScale` | The Space Grotesk family and the phone and tablet scales |
| `internal/TaffyMaterialBridge` | The Material 3 scheme and type scale derived from the tokens |
| `internal/ContrastRatio` | The ratio the unit test asserts against |

## Verification list

No open gaps. The product typeface is vendored
(`res/font/taffy_space_grotesk_variable.ttf`, provenance and checksum in
`vendor/space-grotesk.txt`, held against the file by
`./tools/check fast --only files`)
and registered through explicit 400, 500, 600, and 700 Android font resources
so a device cannot silently render the variable font's light default,
and the token values preserve the shipped Compose palette. The palette is
described by
record 0025,
while `taffy-core/resources/tokens/tokens.json` is its single machine owner.
That source generates the Kotlin projection used here plus future CSS and C++
projections; `ContrastRatioTest` holds the per-surface pairings any re-hue must
keep passing.
