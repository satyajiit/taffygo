// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test
import taffy.core_api.CoreAvailability
import taffy.core_api.LibraryAvailability
import taffy.core_api.LibraryEntryView
import taffy.core_api.LibraryExportView
import taffy.core_api.LibraryRefreshPreviewView
import taffy.core_api.LibraryRefreshSourceView
import taffy.core_api.LibrarySearchView
import taffy.core_api.LibraryViewState
import taffy.core_api.WorkspaceFactKind
import taffy.core_api.WorkspaceExportFormat
import taffy.core_api.TaskProviderRoute

/** Pins the cheap version keys that keep unrelated CoreStatus updates out of Library reducers. */
class LibraryProjectionVersionTest {

    @Test
    fun `the same Library revision and search reuse the collection projection`() {
        val library = library(search("search-1", "warranty"), listOf(entry("entry-1")))

        assertEquals(
            librarySnapshotVersion(CoreAvailability.READY, library),
            librarySnapshotVersion(CoreAvailability.READY, library.copy(entries = emptyList())),
        )
    }

    @Test
    fun `availability revision and search identity each invalidate the collection projection`() {
        val library = library(search("search-1", "warranty"))
        val baseline = librarySnapshotVersion(CoreAvailability.READY, library)

        assertNotEquals(
            baseline,
            librarySnapshotVersion(CoreAvailability.UNAVAILABLE, library),
        )
        assertNotEquals(
            baseline,
            librarySnapshotVersion(
                CoreAvailability.READY,
                library.copy(refresh_previews = listOf(refreshPreview())),
            ),
        )
        assertNotEquals(
            baseline,
            librarySnapshotVersion(CoreAvailability.READY, library.copy(revision = 8uL)),
        )
        assertNotEquals(
            baseline,
            librarySnapshotVersion(
                CoreAvailability.READY,
                library.copy(search = search("search-2", "warranty")),
            ),
        )
    }

    @Test
    fun `an exact deterministic export is reused across unrelated status publications`() {
        val library = library(search = null)
        val export = LibraryExportView(
            request_id = "export-1",
            library_revision = 7uL,
            collection_id = "collection-1",
            format = WorkspaceExportFormat.MARKDOWN,
            content = "first allocation",
        )

        assertEquals(
            libraryExportVersion(CoreAvailability.READY, library, export),
            libraryExportVersion(
                CoreAvailability.READY,
                library,
                export.copy(content = "a republished allocation of the same exact artifact"),
            ),
        )
        assertNotEquals(
            libraryExportVersion(CoreAvailability.READY, library, export),
            libraryExportVersion(
                CoreAvailability.READY,
                library,
                export.copy(request_id = "export-2", format = WorkspaceExportFormat.CSV),
            ),
        )
    }

    private fun library(
        search: LibrarySearchView?,
        entries: List<LibraryEntryView> = emptyList(),
    ) = LibraryViewState(
        availability = LibraryAvailability.AVAILABLE,
        revision = 7uL,
        entries = entries,
        search = search,
        refresh_previews = emptyList(),
        refresh_results = emptyList(),
    )

    private fun entry(id: String) = LibraryEntryView(
        entry_id = id,
        revision = 1uL,
        collection_id = "collection-1",
        collection_name = "Research",
        source_workspace_id = "workspace-1",
        source_workspace_revision = 1uL,
        source_fact_id = "fact-1",
        field = "warranty",
        original_value = "two years",
        correction = null,
        kind = WorkspaceFactKind.FROM_PAGE,
        sources = emptyList(),
        captured_at_epoch_ms = 1uL,
        last_checked_epoch_ms = 1uL,
        has_conflict = false,
    )

    private fun search(requestId: String, query: String) = LibrarySearchView(
        request_id = requestId,
        query = query,
        library_revision = 7uL,
        hits = emptyList(),
    )

    private fun refreshPreview() = LibraryRefreshPreviewView(
        preview_id = "a".repeat(64),
        collection_id = "collection-1",
        library_revision = 7uL,
        source_workspace_revision = 2uL,
        provider_route = TaskProviderRoute.NO_MODEL_REQUIRED,
        navigation_count = 1u,
        observation_count = 1u,
        sources = listOf(LibraryRefreshSourceView("source-1", "Page", "example.test")),
    )
}
