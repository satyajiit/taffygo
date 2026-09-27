// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.AssistantMode

/**
 * The mode chip of UX spec section 3.
 *
 * It draws nothing at all in the default mode, which is what "no mode chip"
 * means, and it announces every change — the specification's section 12.1
 * obligation, met by the chip rather than by each screen that shows one.
 */
@Composable
fun TaffyModeChip(
    mode: AssistantMode,
    modifier: Modifier = Modifier,
) {
    if (!mode.showsChip) return

    val label = taffyString(taffyModeLabel(mode))
    Text(
        text = label,
        style = TaffyTheme.typography.label,
        color = TaffyTheme.colors.accent,
        modifier = modifier
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.accent, TaffyTheme.shapes.pill)
            .padding(horizontal = TaffyTheme.spacing.snug, vertical = TaffyTheme.spacing.step)
            .semantics {
                liveRegion = LiveRegionMode.Polite
                stateDescription = label
            },
    )
}

/**
 * One localized name per mode, for the chip and for the surfaces that speak the
 * mode without drawing it.
 *
 * The assistant pill is the second caller and the reason this is not private:
 * its line is the whole of the bottom bar now, so the mode reaches a screen
 * reader through the pill's own state description rather than through a chip
 * beside it (decision 0141), and two spellings of "Taffy browses" is exactly
 * the drift a shared mapping prevents.
 */
@StringRes
fun taffyModeLabel(mode: AssistantMode): Int = when (mode) {
    AssistantMode.YOU_BROWSE -> R.string.taffy_mode_you_browse
    AssistantMode.BROWSE_TOGETHER -> R.string.taffy_mode_browse_together
    AssistantMode.TAFFY_BROWSES -> R.string.taffy_mode_taffy_browses
}
