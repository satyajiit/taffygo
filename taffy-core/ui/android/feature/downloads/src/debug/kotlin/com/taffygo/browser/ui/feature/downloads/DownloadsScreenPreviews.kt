// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import com.taffygo.browser.ui.core.ui.TaffyPreview

@Preview(name = "Downloads organizer - light", showBackground = true, widthDp = 393, heightDp = 852)
@Composable
private fun DownloadsOrganizerLightPreview() {
    TaffyPreview(darkTheme = false, reducedMotion = false) {
        DownloadsContent(state = DownloadPreviewStates.organizer, onIntent = {})
    }
}

@Preview(name = "Downloads organizer - dark", showBackground = true, widthDp = 393, heightDp = 852)
@Composable
private fun DownloadsOrganizerDarkPreview() {
    TaffyPreview(darkTheme = true, reducedMotion = true) {
        DownloadsContent(state = DownloadPreviewStates.organizer, onIntent = {})
    }
}
