// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun BrowserStartAreaPreview() {
    TaffyPreview(darkTheme = false) {
        BrowserStartArea(
            frequent = PreviewStates.newTab.frequent,
            onOpenSite = {},
            composer = previewStartComposer(),
        )
    }
}

@ThemePreviews
@Composable
private fun BrowserStartAreaDarkPreview() {
    TaffyPreview(darkTheme = true) {
        BrowserStartArea(
            frequent = PreviewStates.newTab.frequent,
            onOpenSite = {},
            composer = previewStartComposer(),
        )
    }
}

/**
 * The state a device is actually in on a first run.
 *
 * The frequent grid is the person's own visit counts and a fresh profile has
 * none, so the body is the greeting, the box, and one quiet line. It is the
 * state worth looking at hardest, because it is the one every new install
 * sees.
 */
@ThemePreviews
@Composable
private fun BrowserStartAreaEmptyDarkPreview() {
    TaffyPreview(darkTheme = true) {
        BrowserStartArea(
            frequent = emptyList(),
            onOpenSite = {},
            composer = previewStartComposer(),
        )
    }
}

@FontScalePreviews
@Composable
private fun BrowserStartAreaEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        BrowserStartArea(
            frequent = emptyList(),
            onOpenSite = {},
            composer = previewStartComposer(),
        )
    }
}
