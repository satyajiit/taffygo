// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState

/** Fixed organizer state for comparable previews. */
internal object DownloadPreviewStates {
    private val records = listOf(
        DownloadRecord(
            DownloadId("dl_1"),
            "retention-policy.pdf",
            "docs.example.test",
            2_400_000,
            2_400_000,
            DownloadState.COMPLETE,
            setOf(DownloadAction.OPEN, DownloadAction.SHARE, DownloadAction.REMOVE),
        ),
        DownloadRecord(
            DownloadId("dl_2"),
            "price-list.csv",
            "shop.example.test",
            180_000,
            96_000,
            DownloadState.RUNNING,
            setOf(DownloadAction.PAUSE, DownloadAction.CANCEL),
        ),
        DownloadRecord(
            DownloadId("dl_3"),
            "comparison.md",
            "reviews.example.test",
            null,
            0,
            DownloadState.FAILED,
            setOf(DownloadAction.RESUME, DownloadAction.REMOVE),
        ),
    )
    private val projection = DownloadOrganizerIndex.build(records).project(
        query = "",
        filter = DownloadFilter.ALL,
        sort = DownloadSort.NEWEST,
        grouping = DownloadGrouping.STATUS,
    )
    val organizer = DownloadsUiState(
        grouping = DownloadGrouping.STATUS,
        collectionStatus = DownloadCollectionStatus.COMPLETE,
        groups = projection.groups,
        totalCount = projection.totalCount,
        visibleCount = projection.visibleCount,
    )
}
