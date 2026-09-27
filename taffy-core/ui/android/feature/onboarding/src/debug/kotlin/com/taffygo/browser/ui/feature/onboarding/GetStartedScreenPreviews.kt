// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TabletPreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/**
 * Debug-only Compose previews; never part of the product APK.
 *
 * There is no screenshot harness in this repository, so these are the only
 * light/dark artifact SCR-007 has. The three states are chosen to be wide of
 * each other: the untouched screen is what a person actually arrives at and
 * is the one whose face is the fallback letter, the answered one is where the
 * monogram has to be the typed name's initials rather than that fallback, and
 * the scaled preview is where a 96 dp face above a field above a horizontal
 * run of 48 dp targets goes wrong if it is going to.
 */
@ThemePreviews
@Composable
private fun GetStartedPreview() {
    TaffyPreview(darkTheme = false) {
        GetStartedContent(state = GetStartedUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun GetStartedAnsweredPreview() {
    TaffyPreview(darkTheme = true) {
        GetStartedContent(
            state = GetStartedUiState(
                name = "Priya Sharma",
                avatar = LocalAvatar.of("a1"),
            ),
            onIntent = {},
        )
    }
}

/** A name with no picture chosen: the face is the letters and nothing else. */
@FontScalePreviews
@Composable
private fun GetStartedScaledPreview() {
    TaffyPreview(darkTheme = false) {
        GetStartedContent(
            state = GetStartedUiState(name = "Ada Lovelace"),
            onIntent = {},
        )
    }
}

@TabletPreviews
@Composable
private fun GetStartedTabletPreview() {
    TaffyPreview(darkTheme = false) {
        GetStartedContent(
            state = GetStartedUiState(name = "Priya", avatar = LocalAvatar.of("b4")),
            onIntent = {},
        )
    }
}
