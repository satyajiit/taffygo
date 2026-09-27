// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.feature.downloads.DownloadCollectionStatus
import com.taffygo.browser.ui.feature.downloads.DownloadRepository
import com.taffygo.browser.ui.feature.downloads.DownloadSnapshot
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

/** Window/profile adapter from the browser's download facts to the feature's single seam. */
@TaffyWindowScope
class BrowserDownloadRepository @Inject constructor(
    private val browser: BrowserRepository,
    lifetime: TaffyWindowLifetime,
) : DownloadRepository {
    override val snapshot: StateFlow<DownloadSnapshot> = combine(
        browser.downloads,
        browser.downloadsReady,
        browser.downloadsComplete,
        browser.downloadsUnavailable,
    ) { downloads, ready, complete, unavailable ->
        browserDownloadSnapshot(downloads, ready, complete, unavailable)
    }.stateIn(
        scope = lifetime.scope,
        started = SharingStarted.Eagerly,
        initialValue = DownloadSnapshot.LOADING,
    )

    override suspend fun perform(id: DownloadId, action: DownloadAction): Boolean {
        if (!downloadActionIsCurrent(browser.downloads.value, id, action)) return false
        return browser.performDownloadAction(id, action)
    }
}

internal fun browserDownloadSnapshot(
    downloads: List<DownloadRecord>,
    ready: Boolean,
    complete: Boolean,
    unavailable: Boolean = false,
): DownloadSnapshot = DownloadSnapshot.bounded(
    downloads = downloads,
    status = when {
        !ready -> DownloadCollectionStatus.LOADING
        unavailable -> DownloadCollectionStatus.UNAVAILABLE
        complete -> DownloadCollectionStatus.COMPLETE
        else -> DownloadCollectionStatus.LIMITED
    },
)

internal fun downloadActionIsCurrent(
    downloads: List<DownloadRecord>,
    id: DownloadId,
    action: DownloadAction,
): Boolean = downloads.firstOrNull { it.id == id }?.let { action in it.allowedActions } ?: false
