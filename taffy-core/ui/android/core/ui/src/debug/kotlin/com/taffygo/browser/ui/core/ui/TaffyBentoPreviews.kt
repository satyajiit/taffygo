// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.TaffyWindowWidth

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffyBentoLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyBentoPreviewBody()
    }
}

@ThemePreviews
@Composable
private fun TaffyBentoDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyBentoPreviewBody()
    }
}

/** The fold: at 200% text every tile below stands in one column. */
@FontScalePreviews
@Composable
private fun TaffyBentoFontScalePreview() {
    TaffyPreview(darkTheme = false) {
        TaffyBentoPreviewBody()
    }
}

/** Four columns across an expanded window. */
@TabletPreviews
@Composable
private fun TaffyBentoTabletPreview() {
    TaffyPreview(darkTheme = false, windowWidth = TaffyWindowWidth.EXPANDED) {
        TaffyBentoPreviewBody()
    }
}

@Composable
private fun TaffyBentoPreviewBody() {
    TaffyBentoGrid(modifier = Modifier.padding(TaffyTheme.spacing.screenMargin)) {
        item(span = TaffyBentoSpan.Full) {
            TaffyHeroCard(
                title = taffyString(R.string.taffy_assistant_ask_taffy),
                eyebrow = taffyString(R.string.taffy_mode_taffy_browses),
                body = taffyString(R.string.taffy_control_take_over),
                wash = TaffyTileWash.RibbonOne.washColor(),
            )
        }
        item {
            TaffyBentoTile(
                title = taffyString(R.string.taffy_assistant_ask_taffy),
                summary = taffyString(R.string.taffy_mode_taffy_browses),
                accessibleDescription = taffyString(R.string.taffy_assistant_ask_taffy),
                icon = TaffyIcon.Sparkle,
                wash = TaffyTileWash.Accent,
                onClick = {},
            )
        }
        item {
            TaffyBentoTile(
                title = taffyString(R.string.taffy_mode_you_browse),
                summary = taffyString(R.string.taffy_control_take_over),
                accessibleDescription = taffyString(R.string.taffy_mode_you_browse),
                icon = TaffyIcon.UserCircle,
                wash = TaffyTileWash.RibbonOne,
                onClick = {},
            )
        }
        item {
            TaffyBentoTile(
                title = taffyString(R.string.taffy_task_state_running),
                accessibleDescription = taffyString(R.string.taffy_task_state_running),
                icon = TaffyIcon.ShieldCheck,
                wash = TaffyTileWash.RibbonTwo,
                value = taffyCount(0),
                onClick = {},
            )
        }
        item {
            TaffyBentoTile(
                title = taffyString(R.string.taffy_mode_browse_together),
                summary = taffyString(R.string.taffy_assistant_review),
                accessibleDescription = taffyString(R.string.taffy_mode_browse_together),
                icon = TaffyIcon.Question,
                wash = TaffyTileWash.RibbonThree,
                selected = true,
                onClick = {},
            )
        }
        item {
            TaffyStatTile(
                value = taffyCount(0),
                label = taffyString(R.string.taffy_control_pause),
                icon = TaffyIcon.ChartBar,
                wash = TaffyTileWash.RibbonTwo,
            )
        }
        item {
            TaffyStatTile(
                value = taffyCount(0),
                label = taffyString(R.string.taffy_control_stop),
                icon = TaffyIcon.Funnel,
                wash = TaffyTileWash.RibbonTwo,
            )
        }
    }
}
