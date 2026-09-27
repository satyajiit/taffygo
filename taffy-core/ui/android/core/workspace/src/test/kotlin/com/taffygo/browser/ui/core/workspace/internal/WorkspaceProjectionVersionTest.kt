// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.workspace.internal

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.TaskTemplateId
import taffy.core_api.WorkspaceExportFormat
import taffy.core_api.WorkspaceExportView
import taffy.core_api.WorkspacePhase
import taffy.core_api.WorkspaceViewState

/** Pins the exact cheap keys that keep unrelated status updates off the UI path. */
internal class WorkspaceProjectionVersionTest {

    @Test
    fun `identity revision saved state and order own the workspace projection`() {
        val first = workspace("first", 7uL)
        val second = workspace("second", 3uL)

        assertTrue(
            sameWorkspaceProjectionVersion(
                listOf(first, second),
                listOf(first.copy(goal = "a republished allocation"), second),
            ),
        )
        assertFalse(
            sameWorkspaceProjectionVersion(
                listOf(first, second),
                listOf(first.copy(revision = 8uL), second),
            ),
        )
        assertFalse(sameWorkspaceProjectionVersion(listOf(first, second), listOf(second, first)))
        assertFalse(
            sameWorkspaceProjectionVersion(
                listOf(first, second),
                listOf(first.copy(saved = false), second),
            ),
        )
    }

    @Test
    fun `export content is reused only under its exact deterministic identity`() {
        val export = WorkspaceExportView(
            request_id = "export-1",
            workspace_id = "workspace-1",
            revision = 7uL,
            format = WorkspaceExportFormat.MARKDOWN,
            content = "first allocation",
        )

        assertEquals(
            workspaceExportVersion(export),
            workspaceExportVersion(export.copy(content = "the same deterministic artifact")),
        )
        assertFalse(
            workspaceExportVersion(export) == workspaceExportVersion(export.copy(revision = 8uL)),
        )
    }

    private fun workspace(id: String, revision: ULong) = WorkspaceViewState(
        workspace_id = id,
        revision = revision,
        goal = "research goal",
        phase = WorkspacePhase.DONE,
        last_updated_epoch_ms = 1uL,
        template_id = TaskTemplateId.SUMMARIZE_EVIDENCE,
        sources = emptyList(),
        facts = emptyList(),
        saved = true,
        display_name = "Research",
        deletion_preview = null,
    )
}
