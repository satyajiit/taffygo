// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Color

/**
 * The semantic colour tokens, mirroring `handoff/DESIGN.md` section 2. Both
 * themes define all of them; no component ever references a raw value.
 */
@Immutable
data class TaffyColors(
    /** The page behind everything. */
    val surface: Color,
    /** A card or a row that sits above the surface. */
    val surfaceRaised: Color,
    /** A recessed field below the surface: idle pill, segmented track. */
    val surfaceSunken: Color,
    /** Decorative borders, dividers, and tonal separation. */
    val outline: Color,
    /** Body and heading text. */
    val textPrimary: Color,
    /** Supporting text: timestamps, counts, hints. */
    val textSecondary: Color,
    /**
     * The smallest text step. It used to be a `surfaceRaised`-only token
     * because that was the one surface the record's values cleared in both
     * themes; in dark it now holds on all four surfaces, and in light on every
     * one but `surfaceSunken`. That single shortfall is deliberate, and the
     * palette records why.
     */
    val textTertiary: Color,
    /** Strokes and marks only — dividers, inactive marks. Never text. */
    val hairline: Color,
    /** The fill of a product-shot or thumbnail placeholder. */
    val imagePlaceholder: Color,
    /** The one hue in the UI: Taffy working, waiting, or having touched a value. */
    val accent: Color,
    /** Accent-coloured text on the dark surfaces. */
    val accentText: Color,
    /** Accent-coloured text on the light surfaces. */
    val accentDeep: Color,
    /** The running-pill background: a wash of the accent, never a solid. */
    val accentWash: Color,
    /** Text and icons drawn on top of the accent. */
    val accentOn: Color,
    /** Something failed, or would be destructive. */
    val danger: Color,
    /** Danger-coloured text on a surface or a danger wash. */
    val dangerText: Color,
    /** The danger button fill: a wash of danger, never a solid. */
    val dangerWash: Color,
    /** Something finished, or is in a good state. */
    val positive: Color,
    /** Positive-coloured text on a surface or a positive wash. */
    val positiveText: Color,
    /** The positive chip fill: a wash of positive. */
    val positiveWash: Color,
    /** Something needs attention but nothing is broken. */
    val caution: Color,
    /** The background of a source chip. */
    val sourceChip: Color,
    /** The badge on a fact two sources disagree about. */
    val conflictBadge: Color,
    /** A sheet, which sits above a raised card rather than beside it. */
    val surfaceSheet: Color,
    /** A surface that inverts the theme: a toast, a tooltip, a snackbar. */
    val surfaceInverse: Color,
    /** Text and icons on `surfaceInverse`. */
    val textInverse: Color,
    /** The ring drawn around whatever the keyboard or a switch access is on. */
    val focusRing: Color,
    /** The dim behind a sheet or a dialog. Never a fill for anything else. */
    val scrim: Color,
    /** The fill behind selected text, which stays legible through it. */
    val selection: Color,
    /** The private-tab identity: the ribbon violet, never shared with a hub tile. */
    val privateTint: Color,
    /** The private-tab wash, behind a tinted surface. */
    val privateTintWash: Color,
    /**
     * The solid glyph well of the Taffy tile. The only amber well there is:
     * amber still means Taffy, on a tile as on the pill (decision 0103).
     */
    val accentWell: Color,
    /**
     * The mark's first ribbon hue, solid. A slab on the premium surface and
     * nothing else: a ribbon hue is never a control fill and never text.
     */
    val ribbonOne: Color,
    /** The first ribbon hue's glyph well, on a hub tile that belongs to that group. */
    val ribbonOneWell: Color,
    /** The first ribbon hue's tile ground: a wash over `surfaceRaised`. */
    val ribbonOneWash: Color,
    /** The mark's second ribbon hue, solid. */
    val ribbonTwo: Color,
    /** The second ribbon hue's glyph well. */
    val ribbonTwoWell: Color,
    /** The second ribbon hue's tile ground. */
    val ribbonTwoWash: Color,
    /** The mark's third ribbon hue, solid. */
    val ribbonThree: Color,
    /** The third ribbon hue's glyph well. */
    val ribbonThreeWell: Color,
    /** The third ribbon hue's tile ground. */
    val ribbonThreeWash: Color,
    /** Glyphs drawn on any ribbon well; one ink reads on all three. */
    val ribbonOn: Color,
)

/**
 * The mark's own hues, in the mark's own order, as one ramp.
 *
 * The ribbon of the TaffyGo mark runs cool to warm — the blue of [ribbonOne]
 * into the violet of [ribbonTwo], through the coral of [ribbonThree] and out
 * at the amber of [accent], which is where the mark and the product's one
 * accent are the same colour. Four stops rather than three, because stopping
 * at coral ends the ramp on a hue the mark reaches and passes.
 *
 * It exists because the travelling light on a border is the one moving thing
 * in this product that is *the brand* rather than a state
 * (`drawTaffyBorderComet`), and a single accent stroke says Taffy the way a
 * word does while the ramp says it the way the mark does. This is the one
 * sanctioned use of the ribbon hues as a stroke: decision 0103's rule that a
 * ribbon hue is never a control fill and never text is untouched, since a
 * comet is neither.
 *
 * A new list per read, so callers hoist it into composition rather than
 * building one inside a draw.
 */
val TaffyColors.markRibbon: List<Color>
    get() = listOf(ribbonOne, ribbonTwo, ribbonThree, accent)
