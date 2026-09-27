// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem.internal

/**
 * One theme's token values as colour words.
 *
 * They are plain numbers rather than Compose colours so the contrast check is a
 * unit test on a laptop instead of an instrumentation test on a device. The
 * theme layer wraps them once. Most tokens are opaque; the `*Wash` tokens carry
 * alpha by design (they are washes, composited over a surface at draw time).
 */
internal data class PaletteValues(
    val surface: Long,
    val surfaceRaised: Long,
    val surfaceSunken: Long,
    val outline: Long,
    val textPrimary: Long,
    val textSecondary: Long,
    val textTertiary: Long,
    val hairline: Long,
    val imagePlaceholder: Long,
    val accent: Long,
    val accentText: Long,
    val accentDeep: Long,
    val accentWash: Long,
    val accentOn: Long,
    val danger: Long,
    val dangerText: Long,
    val dangerWash: Long,
    val positive: Long,
    val positiveText: Long,
    val positiveWash: Long,
    val caution: Long,
    val sourceChip: Long,
    val conflictBadge: Long,
    /** A sheet, which sits above a raised card rather than beside it. */
    val surfaceSheet: Long,
    /** A surface that inverts the theme: a toast, a tooltip, a snackbar. */
    val surfaceInverse: Long,
    /** Text and icons on `surfaceInverse`. */
    val textInverse: Long,
    /** The ring drawn around whatever the keyboard or a switch access is on. */
    val focusRing: Long,
    /** The dim behind a sheet or a dialog. Never a fill for anything else. */
    val scrim: Long,
    /** The fill behind selected text, which stays legible through it. */
    val selection: Long,
    /** The private-tab identity: the ribbon violet, never shared with a hub tile. */
    val privateTint: Long,
    /** The private-tab wash, behind a tinted surface. */
    val privateTintWash: Long,
    /** The solid glyph well of the Taffy tile (decision 0103). */
    val accentWell: Long,
    /** The mark's first ribbon hue, solid: a premium slab. */
    val ribbonOne: Long,
    /** The first ribbon hue's glyph well. */
    val ribbonOneWell: Long,
    /** The first ribbon hue's tile ground, over `surfaceRaised`. */
    val ribbonOneWash: Long,
    /** The mark's second ribbon hue, solid. */
    val ribbonTwo: Long,
    /** The second ribbon hue's glyph well. */
    val ribbonTwoWell: Long,
    /** The second ribbon hue's tile ground. */
    val ribbonTwoWash: Long,
    /** The mark's third ribbon hue, solid. */
    val ribbonThree: Long,
    /** The third ribbon hue's glyph well. */
    val ribbonThreeWell: Long,
    /** The third ribbon hue's tile ground. */
    val ribbonThreeWash: Long,
    /** Glyphs on any ribbon well. */
    val ribbonOn: Long,
) {
    /** Every token, named, so a check can walk them instead of listing them. */
    fun asPairs(): List<Pair<String, Long>> = listOf(
        "surface" to surface,
        "surfaceRaised" to surfaceRaised,
        "surfaceSunken" to surfaceSunken,
        "outline" to outline,
        "textPrimary" to textPrimary,
        "textSecondary" to textSecondary,
        "textTertiary" to textTertiary,
        "hairline" to hairline,
        "imagePlaceholder" to imagePlaceholder,
        "accent" to accent,
        "accentText" to accentText,
        "accentDeep" to accentDeep,
        "accentWash" to accentWash,
        "accentOn" to accentOn,
        "danger" to danger,
        "dangerText" to dangerText,
        "dangerWash" to dangerWash,
        "positive" to positive,
        "positiveText" to positiveText,
        "positiveWash" to positiveWash,
        "caution" to caution,
        "sourceChip" to sourceChip,
        "conflictBadge" to conflictBadge,
        "surfaceSheet" to surfaceSheet,
        "surfaceInverse" to surfaceInverse,
        "textInverse" to textInverse,
        "focusRing" to focusRing,
        "scrim" to scrim,
        "selection" to selection,
        "privateTint" to privateTint,
        "privateTintWash" to privateTintWash,
        "accentWell" to accentWell,
        "ribbonOne" to ribbonOne,
        "ribbonOneWell" to ribbonOneWell,
        "ribbonOneWash" to ribbonOneWash,
        "ribbonTwo" to ribbonTwo,
        "ribbonTwoWell" to ribbonTwoWell,
        "ribbonTwoWash" to ribbonTwoWash,
        "ribbonThree" to ribbonThree,
        "ribbonThreeWell" to ribbonThreeWell,
        "ribbonThreeWash" to ribbonThreeWash,
        "ribbonOn" to ribbonOn,
    )
}
