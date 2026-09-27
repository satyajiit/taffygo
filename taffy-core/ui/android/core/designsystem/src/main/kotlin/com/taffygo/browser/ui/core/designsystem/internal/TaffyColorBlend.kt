// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem.internal

import androidx.compose.ui.graphics.lerp
import com.taffygo.browser.ui.core.designsystem.TaffyColors

/**
 * The same token set, part way to another one.
 *
 * A theme change is a cross-fade of the whole palette rather than a cut behind
 * a wash: every surface, every word and every border travels from the outgoing
 * value to the incoming one together, so nothing on screen disappears and
 * reappears. That is what makes the change read as one movement instead of a
 * blink with a rising sun drawn over it.
 *
 * Every field is named. A token added to [TaffyColors] and forgotten here would
 * hold its outgoing value for the whole change and then snap; `TaffyColorBlendTest`
 * is what refuses to let that happen, by asserting the blend reproduces each
 * scheme exactly at its own end.
 */
internal fun TaffyColors.blendTo(other: TaffyColors, fraction: Float): TaffyColors =
    copy(
        surface = lerp(surface, other.surface, fraction),
        surfaceRaised = lerp(surfaceRaised, other.surfaceRaised, fraction),
        surfaceSunken = lerp(surfaceSunken, other.surfaceSunken, fraction),
        outline = lerp(outline, other.outline, fraction),
        textPrimary = lerp(textPrimary, other.textPrimary, fraction),
        textSecondary = lerp(textSecondary, other.textSecondary, fraction),
        textTertiary = lerp(textTertiary, other.textTertiary, fraction),
        hairline = lerp(hairline, other.hairline, fraction),
        imagePlaceholder = lerp(imagePlaceholder, other.imagePlaceholder, fraction),
        accent = lerp(accent, other.accent, fraction),
        accentText = lerp(accentText, other.accentText, fraction),
        accentDeep = lerp(accentDeep, other.accentDeep, fraction),
        accentWash = lerp(accentWash, other.accentWash, fraction),
        accentOn = lerp(accentOn, other.accentOn, fraction),
        danger = lerp(danger, other.danger, fraction),
        dangerText = lerp(dangerText, other.dangerText, fraction),
        dangerWash = lerp(dangerWash, other.dangerWash, fraction),
        positive = lerp(positive, other.positive, fraction),
        positiveText = lerp(positiveText, other.positiveText, fraction),
        positiveWash = lerp(positiveWash, other.positiveWash, fraction),
        caution = lerp(caution, other.caution, fraction),
        sourceChip = lerp(sourceChip, other.sourceChip, fraction),
        conflictBadge = lerp(conflictBadge, other.conflictBadge, fraction),
        surfaceSheet = lerp(surfaceSheet, other.surfaceSheet, fraction),
        surfaceInverse = lerp(surfaceInverse, other.surfaceInverse, fraction),
        textInverse = lerp(textInverse, other.textInverse, fraction),
        focusRing = lerp(focusRing, other.focusRing, fraction),
        scrim = lerp(scrim, other.scrim, fraction),
        selection = lerp(selection, other.selection, fraction),
        privateTint = lerp(privateTint, other.privateTint, fraction),
        privateTintWash = lerp(privateTintWash, other.privateTintWash, fraction),
        accentWell = lerp(accentWell, other.accentWell, fraction),
        ribbonOne = lerp(ribbonOne, other.ribbonOne, fraction),
        ribbonOneWell = lerp(ribbonOneWell, other.ribbonOneWell, fraction),
        ribbonOneWash = lerp(ribbonOneWash, other.ribbonOneWash, fraction),
        ribbonTwo = lerp(ribbonTwo, other.ribbonTwo, fraction),
        ribbonTwoWell = lerp(ribbonTwoWell, other.ribbonTwoWell, fraction),
        ribbonTwoWash = lerp(ribbonTwoWash, other.ribbonTwoWash, fraction),
        ribbonThree = lerp(ribbonThree, other.ribbonThree, fraction),
        ribbonThreeWell = lerp(ribbonThreeWell, other.ribbonThreeWell, fraction),
        ribbonThreeWash = lerp(ribbonThreeWash, other.ribbonThreeWash, fraction),
        ribbonOn = lerp(ribbonOn, other.ribbonOn, fraction),
    )
