// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceDeletionPreview
import com.taffygo.browser.ui.core.model.WorkspaceId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.LibraryEntryView
import taffy.core_api.LibrarySourceView
import taffy.core_api.WorkspaceFactKind

/** Proves a Library collection carries its backing workspace's mutation facts. */
class LibraryCollectionManagementProjectionTest {

    @Test
    fun `saved workspace supplies exact rename and deletion authority`() {
        val preview = WorkspaceDeletionPreview(
            sources = 2u,
            facts = 3u,
            artifactMetadata = 1u,
            derivedIndexes = 4u,
            confirmationToken = "a".repeat(64),
        )
        val workspace = Workspace(
            id = WorkspaceId("workspace-1"),
            goal = "Compare the evidence",
            state = TaskDisplayState.DONE,
            lastUpdatedEpochMillis = 1L,
            template = TaskTemplate.COMPARE_PRODUCTS,
            sources = emptyList(),
            facts = emptyList(),
            revision = 9uL,
            displayName = "Renamed evidence",
            deletionPreview = preview,
        )

        val collection = libraryCollection(
            collectionId = "workspace-1",
            entries = listOf(entry()),
            libraryRevision = 17uL,
            workspace = workspace,
        )

        assertEquals("Renamed evidence", collection.name)
        assertEquals("Compare the evidence", collection.goal)
        assertEquals(9uL, collection.revision)
        assertEquals(preview, collection.deletionPreview)
        assertTrue(collection.canManage)
    }

    @Test
    fun `orphaned kept evidence remains visible but cannot mutate a missing workspace`() {
        val collection = libraryCollection(
            collectionId = "workspace-1",
            entries = listOf(entry()),
            libraryRevision = 17uL,
        )

        assertEquals("Original collection", collection.name)
        assertEquals(17uL, collection.revision)
        assertNull(collection.deletionPreview)
        assertFalse(collection.canManage)
    }

    @Test
    fun `production projection keeps exact bounded Library metadata`() {
        val entry = entry().copy(
            captured_at_epoch_ms = 1_724_198_400_000uL,
            last_checked_epoch_ms = 1_724_112_000_000uL,
            has_conflict = true,
            sources = listOf(
                LibrarySourceView("source-1", "Listing", "docs.example.test", 1uL),
                LibrarySourceView("source-2", "Spec", "shop.example.test", 2uL),
            ),
        )

        val collection = libraryCollection(
            collectionId = "workspace-1",
            entries = listOf(entry),
            libraryRevision = 17uL,
        )
        val item = collection.items.single()

        assertEquals("2024-08-21", item.capturedAt)
        assertEquals("Checked 2024-08-20", item.freshness)
        assertEquals("2 cited sources disagree.", item.conflictSummary)
        assertEquals("Checked 2024-08-20", collection.freshness)
        assertTrue(item.conflictSummary.orEmpty().length <= 64)
    }

    private fun entry() = LibraryEntryView(
        entry_id = "entry-1",
        revision = 2uL,
        collection_id = "workspace-1",
        collection_name = "Original collection",
        source_workspace_id = "workspace-1",
        source_workspace_revision = 7uL,
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
}
