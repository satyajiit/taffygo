// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.TaffyPartId

/** Everything screen SCR-203 can be asked to do. */
sealed interface DownloadsIntent {
    data class SetQuery(val query: String) : DownloadsIntent
    data class SelectFilter(val filter: DownloadFilter) : DownloadsIntent
    data class SelectSort(val sort: DownloadSort) : DownloadsIntent
    data class SelectGrouping(val grouping: DownloadGrouping) : DownloadsIntent
    data class SelectTab(val tab: DownloadsTab) : DownloadsIntent

    data class Pause(val id: DownloadId) : DownloadsIntent
    data class Resume(val id: DownloadId) : DownloadsIntent
    data class Cancel(val id: DownloadId) : DownloadsIntent
    data class Open(val id: DownloadId) : DownloadsIntent
    data class Share(val id: DownloadId) : DownloadsIntent
    data class Remove(val id: DownloadId) : DownloadsIntent

    /** Delete one part Taffy fetched for itself. Only a part it would not fetch again is offered this. */
    data class RemovePart(val id: TaffyPartId) : DownloadsIntent
    data object DismissActionNotice : DownloadsIntent
}
