// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.TaffyWindowWidth
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TabletPreviews
import com.taffygo.browser.ui.core.ui.TaffyBrandLockup
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyListRow
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.ThemePreviews
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun NewTabPreview() {
    TaffyPreview(darkTheme = false) {
        NewTabContent(
            state = PreviewStates.newTab,
            onIntent = {},
            composer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun NewTabDarkPreview() {
    TaffyPreview(darkTheme = true) {
        NewTabContent(
            state = PreviewStates.newTab,
            onIntent = {},
            composer = previewStartComposer(),
        )
    }
}

@TabletPreviews
@Composable
private fun NewTabTabletPreview() {
    TaffyPreview(darkTheme = false, windowWidth = TaffyWindowWidth.EXPANDED) {
        NewTabContent(
            state = PreviewStates.newTab,
            onIntent = {},
            composer = previewStartComposer(),
        )
    }
}

/**
 * The page with words in the box: the state the welcome folds away for.
 *
 * The box is typed into where it stands, so the lockup, the greeting and the
 * tiles leave and the reading takes the room they were using — which is the
 * layout worth looking at hardest, because it is the one the keyboard is up
 * for and the one no idle screenshot shows.
 */
@ThemePreviews
@Composable
private fun NewTabTypingPreview() {
    TaffyPreview(darkTheme = false) {
        NewTabContent(
            state = PreviewStates.newTab,
            onIntent = {},
            composer = previewStartComposer(PreviewStates.addressBar),
        )
    }
}

@FontScalePreviews
@Composable
private fun NewTabEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        NewTabContent(
            state = NewTabUiState(startPageGate = StartPageGate(ready = true)),
            onIntent = {},
            composer = previewStartComposer(),
        )
    }
}
