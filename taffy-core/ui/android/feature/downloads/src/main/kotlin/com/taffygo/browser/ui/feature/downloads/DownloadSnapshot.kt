// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.DownloadRecord

/** One immutable, bounded reading of a profile's download store. */
@ConsistentCopyVisibility
data class DownloadSnapshot private constructor(
    val downloads: List<DownloadRecord>,
    val status: DownloadCollectionStatus,
) {
    companion object {
        val LOADING: DownloadSnapshot = DownloadSnapshot(
            downloads = emptyList(),
            status = DownloadCollectionStatus.LOADING,
        )

        fun bounded(
            downloads: List<DownloadRecord>,
            status: DownloadCollectionStatus,
        ): DownloadSnapshot {
            val visible = downloads.take(MAX_ORGANIZER_DOWNLOADS).toList()
            return DownloadSnapshot(
                downloads = visible,
                status = when {
                    status == DownloadCollectionStatus.UNAVAILABLE -> status
                    downloads.size > visible.size -> DownloadCollectionStatus.LIMITED
                    else -> status
                },
            )
        }
    }
}

/** The browsing contract's maximum profile-download projection. */
const val MAX_ORGANIZER_DOWNLOADS: Int = 256
