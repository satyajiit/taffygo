// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.ThemePreviews
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyString

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun BrowserNoticePreview() {
    TaffyPreview(darkTheme = false) {
        BrowserNoticeCard(notice = BrowserNotice.NO_SEARCH_ENGINE, onDismiss = {})
    }
}

@ThemePreviews
@Composable
private fun BrowserNoticeDarkPreview() {
    TaffyPreview(darkTheme = true) {
        BrowserNoticeCard(notice = BrowserNotice.NO_SEARCH_ENGINE, onDismiss = {})
    }
}

@FontScalePreviews
@Composable
private fun BrowserNoticeFontScalePreview() {
    TaffyPreview(darkTheme = false) {
        BrowserNoticeCard(notice = BrowserNotice.NO_SEARCH_ENGINE, onDismiss = {})
    }
}
