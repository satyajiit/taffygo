// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffyPageChipLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyPageChipStates()
    }
}

@ThemePreviews
@Composable
private fun TaffyPageChipDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyPageChipStates()
    }
}

@Composable
private fun TaffyPageChipStates() {
    val opened = taffyString(R.string.taffy_assistant_ask_taffy)
    Column(
        modifier = Modifier.padding(TaffyTheme.spacing.tight),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffyPageChip(
            state = TaffyPageChipState(title = PreviewTitle, host = PreviewHost),
            taffyOpenedCaption = opened,
            closedCaption = PreviewClosed,
            removeContentDescription = PreviewRemove,
            onRemove = {},
        )
        TaffyPageChip(
            state = TaffyPageChipState(
                title = PreviewTitle,
                host = PreviewHost,
                taffyOpened = true,
                removable = false,
            ),
            taffyOpenedCaption = opened,
            closedCaption = PreviewClosed,
            removeContentDescription = PreviewRemove,
        )
        TaffyPageChip(
            state = TaffyPageChipState(
                title = PreviewTitle,
                host = PreviewHost,
                closed = true,
            ),
            taffyOpenedCaption = opened,
            closedCaption = PreviewClosed,
            removeContentDescription = PreviewRemove,
            onRemove = {},
        )
    }
}

private const val PreviewTitle = "Sony Bravia 98-inch listing"
private const val PreviewHost = "croma.com"
private const val PreviewClosed = "This tab closed"
private const val PreviewRemove = "Remove listing"
