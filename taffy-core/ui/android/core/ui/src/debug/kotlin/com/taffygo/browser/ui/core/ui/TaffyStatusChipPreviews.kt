// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.taffyEaseInOut
import com.taffygo.browser.ui.core.designsystem.taffyPingPongPhase
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskDisplayState

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffyStatusChipLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyStatusChipStates()
    }
}

@ThemePreviews
@Composable
private fun TaffyStatusChipDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyStatusChipStates()
    }
}

/** The seven task states, the way the design document rows them. */
@Composable
private fun TaffyStatusChipStates() {
    Column(
        modifier = Modifier.padding(TaffyTheme.spacing.tight),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaskDisplayState.entries.forEach {
            TaffyStatusChip(presentation = StatusPresentation.of(it))
        }
    }
}
