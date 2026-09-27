// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun PageFailureOfflinePreview() {
    TaffyPreview(darkTheme = false) {
        PageFailureNotice(failure = PageLoadFailure.OFFLINE, onReload = {})
    }
}

@ThemePreviews
@Composable
private fun PageFailureDarkPreview() {
    TaffyPreview(darkTheme = true) {
        PageFailureNotice(failure = PageLoadFailure.TIMED_OUT, onReload = {})
    }
}

@ThemePreviews
@Composable
private fun PageFailureUnreachablePreview() {
    TaffyPreview(darkTheme = false) {
        PageFailureNotice(failure = PageLoadFailure.UNREACHABLE, onReload = {})
    }
}

@ThemePreviews
@Composable
private fun PageFailureCrashedPreview() {
    TaffyPreview(darkTheme = false) {
        PageFailureNotice(failure = PageLoadFailure.PAGE_CRASHED, onReload = {})
    }
}

@FontScalePreviews
@Composable
private fun PageFailureCertificatePreview() {
    TaffyPreview(darkTheme = false) {
        PageFailureNotice(failure = PageLoadFailure.CERTIFICATE_INVALID, onReload = {})
    }
}
