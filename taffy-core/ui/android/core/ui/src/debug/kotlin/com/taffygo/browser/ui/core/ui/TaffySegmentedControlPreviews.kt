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
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffySegmentedControlLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffySegmentedControlPreviewContent()
    }
}

@ThemePreviews
@Composable
private fun TaffySegmentedControlDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffySegmentedControlPreviewContent()
    }
}

@Composable
private fun TaffySegmentedControlPreviewContent() {
    val system = "System"
    val light = "Light"
    val dark = "Dark"
    Box(modifier = Modifier.padding(TaffyTheme.spacing.tight)) {
        TaffySegmentedControl(
            options = listOf(system, light, dark),
            selectedIndex = 0,
            onSelect = {},
        )
    }
}
