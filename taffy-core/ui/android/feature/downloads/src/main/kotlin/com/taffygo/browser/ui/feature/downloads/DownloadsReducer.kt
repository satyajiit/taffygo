// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartsState

/** Pure projection of indexed person files and TaffyGo's separately owned parts. */
internal fun projectDownloads(
    organizer: ProjectedDownloadCollection,
    parts: TaffyPartsState = TaffyPartsState(),
    liveProgress: Map<TaffyPartId, TaffyPartProgress> = emptyMap(),
    tab: DownloadsTab = DownloadsTab.YOURS,
    actionNotice: DownloadActionNotice? = null,
): DownloadsUiState = DownloadsUiState(
    tab = tab,
    query = organizer.query,
    filter = organizer.filter,
    sort = organizer.sort,
    grouping = organizer.grouping,
    collectionStatus = organizer.status,
    groups = organizer.projection.groups,
    totalCount = organizer.projection.totalCount,
    visibleCount = organizer.projection.visibleCount,
    actionNotice = actionNotice,
    partsSupported = parts.supported,
    parts = parts.parts.map { part -> part.withLiveProgress(liveProgress[part.id]) },
)

internal data class ProjectedDownloadCollection(
    val query: String,
    val filter: DownloadFilter,
    val sort: DownloadSort,
    val grouping: DownloadGrouping,
    val status: DownloadCollectionStatus,
    val projection: DownloadOrganizerProjection,
)

private fun TaffyPart.withLiveProgress(progress: TaffyPartProgress?): TaffyPart {
    if (progress == null || progress.version != version) return this
    if (progress.downloadedBytes <= downloadedBytes) return this
    return copy(downloadedBytes = progress.downloadedBytes)
}
