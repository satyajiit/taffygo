// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun NotificationsPreview() {
    TaffyPreview(darkTheme = false) {
        NotificationsContent(
            state = NotificationsUiState(enabled = setOf(NotificationTopic.TASK_PROGRESS)),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun NotificationsDarkPreview() {
    TaffyPreview(darkTheme = true) {
        NotificationsContent(
            state = NotificationsUiState(enabled = NotificationTopic.entries.toSet()),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun NotificationsAllOffPreview() {
    TaffyPreview(darkTheme = false) {
        NotificationsContent(state = NotificationsUiState(), onIntent = {})
    }
}
