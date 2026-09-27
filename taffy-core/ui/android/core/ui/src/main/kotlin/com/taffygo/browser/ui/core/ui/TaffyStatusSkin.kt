// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Color
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The treatment one status wears: a ground, a border, and one ink for both the
 * word and the shape that repeats it.
 *
 * It left [TaffyStatusChip] when the assistant pill started wearing the four
 * final treatments. Two tables would have drifted the first time a wash moved,
 * and a 26 dp chip saying Done in one green beside a 48 dp pill saying it in
 * another is one state with two appearances — which is the thing parity row
 * PAR-A11Y-004 is about.
 */
@Immutable
internal class TaffyStatusSkin(
    val background: Color,
    val border: Color?,
    val content: Color,
)

/**
 * Accent-coloured ink: the deep step on paper, the text step on warm black.
 *
 * The handoff names no dark running chip, so the pair follows the pill the chip
 * sits beside.
 */
@Composable
internal fun taffyAccentInk(): Color =
    if (TaffyTheme.isDark) TaffyTheme.colors.accentText else TaffyTheme.colors.accentDeep

/** The design document's seven task-state treatments, in one place. */
@Composable
internal fun taffyStatusSkin(style: TaffyStatusStyle): TaffyStatusSkin {
    val colors = TaffyTheme.colors
    val accentInk = taffyAccentInk()
    return when (style) {
        TaffyStatusStyle.RUNNING ->
            TaffyStatusSkin(colors.accentWash, accentInk.copy(alpha = RunningBorderAlpha), accentInk)
        TaffyStatusStyle.WAITING ->
            TaffyStatusSkin(colors.accent, null, colors.accentOn)
        TaffyStatusStyle.PAUSED ->
            TaffyStatusSkin(Color.Transparent, colors.hairline, colors.textSecondary)
        TaffyStatusStyle.DONE ->
            TaffyStatusSkin(colors.positiveWash, null, colors.positiveText)
        TaffyStatusStyle.PARTLY_DONE ->
            TaffyStatusSkin(colors.accentWash, null, accentInk)
        TaffyStatusStyle.STOPPED ->
            TaffyStatusSkin(colors.surfaceSunken, null, colors.textSecondary)
        TaffyStatusStyle.FAILED ->
            TaffyStatusSkin(colors.dangerWash, null, colors.dangerText)
    }
}

private const val RunningBorderAlpha = 0.45f
