// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem.internal

import androidx.compose.ui.graphics.Color
import com.taffygo.browser.ui.core.designsystem.TaffyColors

/** Turns the two palettes into the token sets a theme hands down the tree. */
internal object TaffyColorSchemes {

    /** The light token set. */
    val light: TaffyColors = TaffyPalette.light.toColors()

    /** The dark token set. */
    val dark: TaffyColors = TaffyPalette.dark.toColors()

    private fun PaletteValues.toColors() = TaffyColors(
        surface = Color(surface),
        surfaceRaised = Color(surfaceRaised),
        surfaceSunken = Color(surfaceSunken),
        outline = Color(outline),
        textPrimary = Color(textPrimary),
        textSecondary = Color(textSecondary),
        textTertiary = Color(textTertiary),
        hairline = Color(hairline),
        imagePlaceholder = Color(imagePlaceholder),
        accent = Color(accent),
        accentText = Color(accentText),
        accentDeep = Color(accentDeep),
        accentWash = Color(accentWash),
        accentOn = Color(accentOn),
        danger = Color(danger),
        dangerText = Color(dangerText),
        dangerWash = Color(dangerWash),
        positive = Color(positive),
        positiveText = Color(positiveText),
        positiveWash = Color(positiveWash),
        caution = Color(caution),
        sourceChip = Color(sourceChip),
        conflictBadge = Color(conflictBadge),
        surfaceSheet = Color(surfaceSheet),
        surfaceInverse = Color(surfaceInverse),
        textInverse = Color(textInverse),
        focusRing = Color(focusRing),
        scrim = Color(scrim),
        selection = Color(selection),
        privateTint = Color(privateTint),
        privateTintWash = Color(privateTintWash),
        accentWell = Color(accentWell),
        ribbonOne = Color(ribbonOne),
        ribbonOneWell = Color(ribbonOneWell),
        ribbonOneWash = Color(ribbonOneWash),
        ribbonTwo = Color(ribbonTwo),
        ribbonTwoWell = Color(ribbonTwoWell),
        ribbonTwoWash = Color(ribbonTwoWash),
        ribbonThree = Color(ribbonThree),
        ribbonThreeWell = Color(ribbonThreeWell),
        ribbonThreeWash = Color(ribbonThreeWash),
        ribbonOn = Color(ribbonOn),
    )
}
