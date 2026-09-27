// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.model.FactKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.TaskTemplateId
import taffy.core_api.WorkspaceFactKind
import taffy.core_api.WorkspaceFactView
import taffy.core_api.WorkspacePhase
import taffy.core_api.WorkspaceSourceView
import taffy.core_api.WorkspaceViewState

internal class TaskOutputProjectionTest {
    /**
     * Sources and facts, and no timeline.
     *
     * This projection used to derive one, and the derivation is what decision
     * 0148 removed: a picture of what a task saved is not a picture of what it
     * did. What it still owes the timeline is the per-source fact count, which
     * is joined by the mapping — see [CoreTaskActivityTimelineTest].
     */
    @Test
    fun `committed workspace reaches its sources and facts and derives no steps`() {
        val projection = workspace(
            sources = listOf(
                source(id = "source-old", host = "old.example", readAt = 10u, facts = 1u),
                source(id = "source-new", host = "new.example", readAt = 30u, facts = 2u),
            ),
            facts = listOf(fact()),
        ).toTaskOutputProjectionOrNull()

        requireNotNull(projection)
        assertEquals(listOf("source-old", "source-new"), projection.sources.map { it.id.value })
        assertEquals(listOf(1, 2), projection.sources.map { it.factCount })
        assertEquals(FactKind.FROM_THE_PAGE, projection.facts.single().kind)
    }

    @Test
    fun `one overflowing source refuses the whole joined projection`() {
        val projection = workspace(
            sources = listOf(
                source(id = "source-good", host = "good.example", readAt = 10u, facts = 1u),
                source(
                    id = "source-bad",
                    host = "bad.example",
                    readAt = ULong.MAX_VALUE,
                    facts = 1u,
                ),
            ),
            facts = listOf(fact()),
        ).toTaskOutputProjectionOrNull()

        assertNull(projection)
    }

    @Test
    fun `only an unsaved terminal workspace offers exact explicit save`() {
        val draft = workspace(
            sources = emptyList(),
            facts = emptyList(),
            phase = WorkspacePhase.PARTLY_DONE,
            saved = false,
        ).toTaskOutputProjectionOrNull()
        requireNotNull(draft)
        assertEquals("workspace-1", draft.workspaceId)
        assertEquals(4uL, draft.workspaceRevision)
        assertFalse(draft.saved)
        assertTrue(draft.canSave)
        assertTrue(draft.canDiscard)

        val saved = workspace(
            sources = emptyList(),
            facts = emptyList(),
            phase = WorkspacePhase.DONE,
            saved = true,
        ).toTaskOutputProjectionOrNull()
        requireNotNull(saved)
        assertFalse(saved.canSave)
        assertFalse(saved.canDiscard)

        val runningDraft = workspace(
            sources = emptyList(),
            facts = emptyList(),
            phase = WorkspacePhase.RUNNING,
            saved = false,
        ).toTaskOutputProjectionOrNull()
        requireNotNull(runningDraft)
        assertFalse(runningDraft.canSave)
        assertFalse(runningDraft.canDiscard)
    }

    private fun workspace(
        sources: List<WorkspaceSourceView>,
        facts: List<WorkspaceFactView>,
        phase: WorkspacePhase = WorkspacePhase.RUNNING,
        saved: Boolean = true,
    ) = WorkspaceViewState(
        workspace_id = "workspace-1",
        revision = 4u,
        goal = "compare the evidence",
        phase = phase,
        last_updated_epoch_ms = 40u,
        template_id = TaskTemplateId.COMPARE_PRODUCTS,
        sources = sources,
        facts = facts,
        saved = saved,
        display_name = "Evidence comparison",
        deletion_preview = null,
    )

    private fun source(
        id: String,
        host: String,
        readAt: ULong,
        facts: UInt,
    ) = WorkspaceSourceView(
        source_id = id,
        title = host,
        host = host,
        read_at_epoch_ms = readAt,
        fact_count = facts,
        excluded = false,
    )

    private fun fact() = WorkspaceFactView(
        fact_id = "fact-1",
        field = "price",
        value = "42",
        kind = WorkspaceFactKind.FROM_PAGE,
        sources = listOf("source-new"),
        correction = null,
        has_conflict = false,
        needs_new_source = false,
    )
}
