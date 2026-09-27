// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.designsystem.TaffyWindowWidth
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TabletPreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TabSwitcherPreview() {
    TaffyPreview(darkTheme = false) {
        TabSwitcherContent(state = PreviewStates.tabSwitcher, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun TabSwitcherDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TabSwitcherContent(
            state = PreviewStates.tabSwitcher.copy(taffyGroupExpanded = true),
            onIntent = {},
        )
    }
}

@TabletPreviews
@Composable
private fun TabSwitcherTabletPreview() {
    TaffyPreview(darkTheme = false, windowWidth = TaffyWindowWidth.EXPANDED) {
        TabSwitcherContent(
            state = PreviewStates.tabSwitcher.copy(taffyGroupExpanded = true),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun TabSwitcherPrivatePreview() {
    TaffyPreview(darkTheme = false) {
        TabSwitcherContent(
            state = PreviewStates.tabSwitcher.copy(group = TabSwitcherGroup.PRIVATE),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun TabSwitcherEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        TabSwitcherContent(state = TabSwitcherUiState(), onIntent = {})
    }
}
