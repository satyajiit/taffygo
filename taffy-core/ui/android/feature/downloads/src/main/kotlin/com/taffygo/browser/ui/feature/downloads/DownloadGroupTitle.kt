// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.DownloadState

/** Structured group title; the surface owns its localized words. */
sealed interface DownloadGroupTitle {
    data object All : DownloadGroupTitle
    data class Status(val state: DownloadState) : DownloadGroupTitle
    data class FileType(val type: DownloadFileType) : DownloadGroupTitle
    data class Source(val host: String) : DownloadGroupTitle
}
