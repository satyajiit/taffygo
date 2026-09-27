// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import taffy.core_api.LibraryRefreshDisposition
import taffy.core_api.LibraryRefreshPreviewView
import taffy.core_api.LibraryRefreshResultView
import taffy.core_api.TaskProviderRoute

/** Maps a generated content-free preview without exposing its reopen locators. */
internal fun LibraryRefreshPreviewView.toLibraryRefreshPreview(): LibraryRepository.RefreshPreview? {
    if (provider_route != TaskProviderRoute.NO_MODEL_REQUIRED || sources.isEmpty()) return null
    return LibraryRepository.RefreshPreview(
        previewId = preview_id,
        collectionId = collection_id,
        libraryRevision = library_revision,
        workspaceRevision = source_workspace_revision,
        navigationCount = navigation_count,
        observationCount = observation_count,
        sources = sources.map { source ->
            LibraryRepository.RefreshSource(
                id = source.source_id,
                title = source.title,
                host = source.host,
            )
        },
    )
}

internal fun LibraryRefreshResultView.toLibraryRefreshResult(): LibraryRepository.RefreshResult =
    LibraryRepository.RefreshResult(
        previewId = preview_id,
        collectionId = collection_id,
        items = items.map { item ->
            LibraryRepository.RefreshResultItem(
                sourceId = item.source_id,
                disposition = when (item.disposition) {
                    LibraryRefreshDisposition.CHANGED ->
                        LibraryRepository.RefreshDisposition.CHANGED
                    LibraryRefreshDisposition.MISSING ->
                        LibraryRepository.RefreshDisposition.MISSING
                    LibraryRefreshDisposition.UNCHANGED ->
                        LibraryRepository.RefreshDisposition.UNCHANGED
                },
            )
        },
    )
