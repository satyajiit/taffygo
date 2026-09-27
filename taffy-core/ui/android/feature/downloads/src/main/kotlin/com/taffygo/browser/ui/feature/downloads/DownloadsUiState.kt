// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.TaffyPart

/** Immutable state for SCR-203's person files and separately labelled TaffyGo parts. */
data class DownloadsUiState(
    val tab: DownloadsTab = DownloadsTab.YOURS,
    val query: String = "",
    val filter: DownloadFilter = DownloadFilter.ALL,
    val sort: DownloadSort = DownloadSort.NEWEST,
    val grouping: DownloadGrouping = DownloadGrouping.NONE,
    val collectionStatus: DownloadCollectionStatus = DownloadCollectionStatus.LOADING,
    val groups: List<DownloadGroup> = emptyList(),
    val totalCount: Int = 0,
    val visibleCount: Int = 0,
    val actionNotice: DownloadActionNotice? = null,
    val partsSupported: Boolean = false,
    val parts: List<TaffyPart> = emptyList(),
) {
    val isLoading: Boolean
        get() = collectionStatus == DownloadCollectionStatus.LOADING

    val isUnavailable: Boolean
        get() = collectionStatus == DownloadCollectionStatus.UNAVAILABLE

    val hasNoDownloads: Boolean
        get() = collectionStatus == DownloadCollectionStatus.COMPLETE && totalCount == 0

    val hasNoMatches: Boolean
        get() = !isLoading && !isUnavailable && totalCount > 0 && visibleCount == 0

    val hasNoParts: Boolean
        get() = parts.isEmpty()
}
