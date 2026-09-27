// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.model.TaskTimelineKind
import org.junit.Assert.assertEquals
import org.junit.Test
import taffy.core_api.AssistantConfigurationView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.LibraryAvailability
import taffy.core_api.LibraryViewState
import taffy.core_api.MemoryAvailability
import taffy.core_api.MemoryViewState
import taffy.core_api.PersonalityPresetView
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedDetailsView
import taffy.core_api.SavedSignInsView
import taffy.core_api.TaskActivityKind
import taffy.core_api.TaskActivityView
import taffy.core_api.TaskPhase
import taffy.core_api.TaskTemplateId
import taffy.core_api.TaskViewState
import taffy.core_api.WorkspaceFactKind
import taffy.core_api.WorkspaceFactView
import taffy.core_api.WorkspacePhase
import taffy.core_api.WorkspaceSourceView
import taffy.core_api.WorkspaceViewState

/**
 * The timeline is the task's own record, and this is where it becomes one
 * (decision 0148).
 *
 * Two things are under test and only one of them is a mapping. The nine kinds
 * have to survive the wire one for one, because a kind that arrives as the
 * wrong one renders the wrong sentence and nothing fails. And the count a read
 * carries has to be joined here from the workspace, because the reducer never
 * sees a fact — which is exactly why an errand's read has no count and must
 * still be a step.
 */
internal class CoreTaskActivityTimelineTest {

    @Test
    fun `every published kind reaches the surface as its own kind`() {
        val published = TaskActivityKind.entries.mapIndexed { index, kind ->
            step(sequence = (index + 1).toULong(), kind = kind, host = null)
        }

        val timeline = status(published).toRepositoryState("task-1").task?.timeline.orEmpty()

        assertEquals(
            listOf(
                TaskTimelineKind.OPENED_PAGE,
                TaskTimelineKind.READ_PAGE,
                TaskTimelineKind.PAGE_UNAVAILABLE,
                TaskTimelineKind.MOVE_REFUSED,
                TaskTimelineKind.ASKED_YOU,
                TaskTimelineKind.YOU_ANSWERED,
                TaskTimelineKind.HANDED_BACK,
                TaskTimelineKind.YOU_TOOK_OVER,
                TaskTimelineKind.BUILT_OUTPUT,
            ),
            timeline.map { it.kind }.reversed(),
        )
        assertEquals(TaskActivityKind.entries.size, timeline.size)
    }

    /**
     * The newest step leads, and the sequence never restarts.
     *
     * The core publishes oldest first, because that is the order things
     * happened; the panel draws newest first (UX spec section 6). The reversal
     * is made here and nowhere else, and a surface that read the list the other
     * way round would draw the oldest three steps under the word "Recent".
     */
    @Test
    fun `the newest step leads and the sequence is the core's own`() {
        val timeline = status(
            listOf(
                step(sequence = 7uL, kind = TaskActivityKind.OPENED_PAGE, host = "one.example"),
                step(sequence = 8uL, kind = TaskActivityKind.READ_PAGE, host = "two.example"),
            ),
        ).toRepositoryState("task-1").task?.timeline.orEmpty()

        assertEquals(listOf(8L, 7L), timeline.map { it.sequence })
        assertEquals(listOf("two.example", "one.example"), timeline.map { it.host })
    }

    /**
     * "Read croma.com — found 2 prices" is composed from two places, and this
     * is the join.
     */
    @Test
    fun `a read takes its count from the workspace source for that host`() {
        val timeline = status(
            listOf(step(sequence = 1uL, kind = TaskActivityKind.READ_PAGE, host = "new.example")),
            workspace = workspace(),
        ).toRepositoryState("task-1").task?.timeline.orEmpty()

        assertEquals(2, timeline.single().count)
    }

    /** An errand saves nothing, and its read is still a step. */
    @Test
    fun `a read of a host the workspace never recorded keeps no count`() {
        val timeline = status(
            listOf(step(sequence = 1uL, kind = TaskActivityKind.READ_PAGE, host = "other.example")),
            workspace = workspace(),
        ).toRepositoryState("task-1").task?.timeline.orEmpty()

        assertEquals(TaskTimelineKind.READ_PAGE, timeline.single().kind)
        assertEquals(0, timeline.single().count)
    }

    /**
     * A source the person excluded supplies no number.
     *
     * Its facts are out of the output, so counting them in the sentence about
     * the page they came from would contradict the screen directly beneath it.
     */
    @Test
    fun `an excluded source supplies no count`() {
        val timeline = status(
            listOf(step(sequence = 1uL, kind = TaskActivityKind.READ_PAGE, host = "old.example")),
            workspace = workspace(),
        ).toRepositoryState("task-1").task?.timeline.orEmpty()

        assertEquals(0, timeline.single().count)
    }

    private fun step(sequence: ULong, kind: TaskActivityKind, host: String?) = TaskActivityView(
        sequence = sequence,
        kind = kind,
        host = host,
        count = 0u,
        at_epoch_ms = 1_000u + sequence,
    )

    private fun workspace() = WorkspaceViewState(
        workspace_id = "workspace-1",
        revision = 4u,
        goal = "compare the evidence",
        phase = WorkspacePhase.RUNNING,
        last_updated_epoch_ms = 40u,
        template_id = TaskTemplateId.COMPARE_PRODUCTS,
        sources = listOf(
            WorkspaceSourceView(
                source_id = "source-new",
                title = "new",
                host = "new.example",
                read_at_epoch_ms = 30u,
                fact_count = 2u,
                excluded = false,
            ),
            WorkspaceSourceView(
                source_id = "source-old",
                title = "old",
                host = "old.example",
                read_at_epoch_ms = 10u,
                fact_count = 5u,
                excluded = true,
            ),
        ),
        facts = listOf(
            WorkspaceFactView(
                fact_id = "fact-1",
                field = "price",
                value = "42",
                kind = WorkspaceFactKind.FROM_PAGE,
                sources = listOf("source-new"),
                correction = null,
                has_conflict = false,
                needs_new_source = false,
            ),
        ),
        saved = false,
        display_name = "Evidence comparison",
        deletion_preview = null,
    )

    private fun status(
        activity: List<TaskActivityView>,
        workspace: WorkspaceViewState? = null,
    ) = CoreStatus(
        availability = CoreAvailability.READY,
        generation = 3uL,
        active_tasks = listOf(
            TaskViewState(
                task_id = "task-1",
                revision = 2uL,
                phase = TaskPhase.RUNNING,
                progress_basis_points = 0u,
                status_message_key = null,
                failure = null,
                goal = "compare the evidence",
                template_id = TaskTemplateId.COMPARE_PRODUCTS,
                pending_action = null,
                workspace_id = workspace?.workspace_id,
                pending_ask_prompt = null,
                pending_field_value_request = null,
                allowed_controls = emptyList(),
                artifacts = emptyList(),
                activity = activity,
            ),
        ),
        auth_state = null,
        workspaces = listOfNotNull(workspace),
        workspace_export = null,
        asset_delivery = null,
        provider_roster = emptyList(),
        provider_probes = emptyList(),
        provider_models = emptyList(),
        assistant_configuration = AssistantConfigurationView(
            revision = 0uL,
            disabled_abilities = emptyList(),
            preset = PersonalityPresetView.CAREFUL_RESEARCHER,
            pace = 0u,
            length = 1u,
            check_in = 0u,
        ),
        library = LibraryViewState(
            availability = LibraryAvailability.AVAILABLE,
            revision = 0uL,
            entries = emptyList(),
            search = null,
            refresh_previews = emptyList(),
            refresh_results = emptyList(),
        ),
        library_export = null,
        memory = MemoryViewState(
            availability = MemoryAvailability.AVAILABLE,
            revision = 0uL,
            records = emptyList(),
            search = null,
        ),
        saved_sign_ins = SavedSignInsView(
            availability = SavedDataAvailability.UNAVAILABLE,
            revision = 0uL,
            records = emptyList(),
        ),
        saved_details = SavedDetailsView(
            availability = SavedDataAvailability.UNAVAILABLE,
            revision = 0uL,
            people = emptyList(),
        ),
        site_skills = emptyList(),
        builtin_skills = emptyList(),
        projection_mode = CoreStatusProjectionMode.COMPLETE,
        projection_omissions = emptyList(),
    )
}
