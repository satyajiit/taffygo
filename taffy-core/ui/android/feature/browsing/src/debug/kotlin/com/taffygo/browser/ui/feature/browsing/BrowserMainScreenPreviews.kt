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
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.designsystem.TaffyWindowWidth
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TabletPreviews
import com.taffygo.browser.ui.core.ui.TaffyAddressPill
import com.taffygo.browser.ui.core.ui.TaffyBackHandler
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPaneSplit
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TaffyTwoPane
import com.taffygo.browser.ui.core.ui.ThemePreviews
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyString

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun BrowserMainPreview() {
    TaffyPreview(darkTheme = false) {
        BrowserMainContent(
            state = PreviewStates.browserMain,
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainDarkPreview() {
    TaffyPreview(darkTheme = true) {
        BrowserMainContent(
            state = PreviewStates.browserMain,
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@TabletPreviews
@Composable
private fun BrowserMainTabletPreview() {
    TaffyPreview(darkTheme = false, windowWidth = TaffyWindowWidth.EXPANDED) {
        BrowserMainContent(
            state = PreviewStates.browserMain,
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainPrivatePreview() {
    TaffyPreview(darkTheme = false) {
        BrowserMainContent(
            state = PreviewStates.browserMainPrivate,
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainPrivateDarkPreview() {
    TaffyPreview(darkTheme = true) {
        BrowserMainContent(
            state = PreviewStates.browserMainPrivate,
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainEmptyTabDarkPreview() {
    TaffyPreview(darkTheme = true) {
        BrowserMainContent(
            state = PreviewStates.browserMainEmptyTab,
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainEmptyPrivateTabDarkPreview() {
    TaffyPreview(darkTheme = true) {
        BrowserMainContent(
            state = PreviewStates.browserMainEmptyPrivateTab,
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainNoticePreview() {
    TaffyPreview(darkTheme = false) {
        BrowserMainContent(
            state = PreviewStates.browserMain.copy(notice = BrowserNotice.NO_SEARCH_ENGINE),
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainOverflowPreview() {
    TaffyPreview(darkTheme = false) {
        BrowserMainContent(
            state = PreviewStates.browserMain.copy(moreOpen = true),
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainFindPreview() {
    TaffyPreview(darkTheme = false) {
        BrowserMainContent(
            state = PreviewStates.browserMain.copy(
                findInPage = FindInPageUiState(
                    open = true,
                    query = "policy",
                    available = false,
                ),
            ),
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserMainSavePreview() {
    TaffyPreview(darkTheme = false) {
        BrowserMainContent(
            state = PreviewStates.browserMain.copy(
                savePageOpen = true,
                savePage = SavePageUiState(
                    title = "Retention policy",
                    host = "docs.example.test",
                ),
            ),
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}

@FontScalePreviews
@Composable
private fun BrowserMainFailurePreview() {
    TaffyPreview(darkTheme = false) {
        BrowserMainContent(
            state = PreviewStates.browserMain.copy(failure = PageLoadFailure.NAME_NOT_RESOLVED),
            onIntent = {},
            startComposer = previewStartComposer(),
        )
    }
}
