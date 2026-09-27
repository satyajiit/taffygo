// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import taffy.core_api.CoreAvailability
import taffy.core_api.LibraryAvailability
import taffy.core_api.LibraryExportView
import taffy.core_api.LibraryRefreshPreviewView
import taffy.core_api.LibraryRefreshResultView
import taffy.core_api.LibraryViewState
import taffy.core_api.WorkspaceExportFormat

/** The complete set of facts that can change the visible Library collection projection. */
internal data class LibrarySnapshotVersion(
    val coreAvailability: CoreAvailability,
    val availability: LibraryAvailability,
    val revision: ULong,
    val searchRequestId: String?,
    val searchQuery: String?,
    val refreshPreviews: List<LibraryRefreshPreviewView>,
    val refreshResults: List<LibraryRefreshResultView>,
)

internal fun librarySnapshotVersion(
    coreAvailability: CoreAvailability,
    library: LibraryViewState,
): LibrarySnapshotVersion = LibrarySnapshotVersion(
    coreAvailability = coreAvailability,
    availability = library.availability,
    revision = library.revision,
    searchRequestId = library.search?.request_id,
    searchQuery = library.search?.query,
    refreshPreviews = library.refresh_previews,
    refreshResults = library.refresh_results,
)

/** The stable identity of the exact deterministic export held in CoreStatus. */
internal data class LibraryExportVersion(
    val coreAvailability: CoreAvailability,
    val availability: LibraryAvailability,
    val currentRevision: ULong,
    val requestId: String?,
    val exportRevision: ULong?,
    val collectionId: String?,
    val format: WorkspaceExportFormat?,
)

internal fun libraryExportVersion(
    coreAvailability: CoreAvailability,
    library: LibraryViewState,
    export: LibraryExportView?,
): LibraryExportVersion = LibraryExportVersion(
    coreAvailability = coreAvailability,
    availability = library.availability,
    currentRevision = library.revision,
    requestId = export?.request_id,
    exportRevision = export?.library_revision,
    collectionId = export?.collection_id,
    format = export?.format,
)
