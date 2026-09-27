// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CoreFailureCode
import taffy.core_api.TaskPhase

/** The pill follows the task: its counts and its newest step come from the projection. */
class AssistantBarReducerTest {

    @Test
    fun `no task is idle with nothing counted`() {
        val bar = projectAssistantBar(ready(task = null))

        assertNull(bar.taskId)
        assertEquals(0, bar.sourcesRead)
        assertEquals(0, bar.sourcesPlanned)
        assertNull(bar.latestStep)
    }

    @Test
    fun `a task that has named no page yet has no step and no count`() {
        val bar = projectAssistantBar(ready(task = task()))

        assertEquals(TaskDisplayState.RUNNING, bar.state)
        assertEquals(0, bar.sourcesRead)
        assertEquals(0, bar.sourcesPlanned)
        assertNull(bar.latestStep)
    }

    /** The phase the core names and how far along it says it is reach the bar as they are. */
    @Test
    fun `the phase key and the progress are carried through unread`() {
        val bar = projectAssistantBar(
            ready(task = task().copy(statusMessageKey = "task.thinking", progressBasisPoints = 2_500u)),
        )

        assertEquals("task.thinking", bar.statusMessageKey)
        assertEquals(2_500, bar.progressBasisPoints)
        assertEquals(R.string.taffy_assistant_phase_thinking, phaseLine("task.thinking"))
        assertEquals(R.string.taffy_assistant_notice_retrying, phaseLine("task.retrying_provider"))
        assertEquals(R.string.taffy_assistant_notice_blocked_move, phaseLine("task.blocked_move"))
        assertNull(phaseLine("task.running"))
        assertNull(phaseLine(null))
    }

    @Test
    fun `pages read are counted from the timeline and pages planned from the sources`() {
        val read = TaskTimelineEntry(1, TaskTimelineKind.READ_PAGE, "docs.example.test", 3)
        val built = TaskTimelineEntry(2, TaskTimelineKind.BUILT_OUTPUT, count = 3)
        val task = task(
            timeline = listOf(read, built),
            sources = listOf(source("a", "docs.example.test"), source("b", "shop.example.test")),
        )

        val bar = projectAssistantBar(ready(task))

        assertEquals(1, bar.sourcesRead)
        assertEquals(2, bar.sourcesPlanned)
    }

    /**
     * A phone read "Paused — 3 of 1 page read" over an errand. The three is
     * what the task did — hosts its timeline read — and the one is what its
     * workspace listed. An errand sets out to read no fixed number of pages,
     * so it has no denominator at all.
     */
    @Test
    fun `an errand plans no pages whatever its workspace lists`() {
        val errand = task(
            timeline = listOf(
                TaskTimelineEntry(1, TaskTimelineKind.READ_PAGE, "myaadhaar.uidai.gov.in", 0),
                TaskTimelineEntry(2, TaskTimelineKind.READ_PAGE, "www.google.com", 0),
                TaskTimelineEntry(3, TaskTimelineKind.READ_PAGE, "uidai.gov.in", 0),
            ),
            sources = listOf(source("a", "myaadhaar.uidai.gov.in")),
            phase = TaskPhase.PAUSED,
            template = TaskTemplate.WEB_ERRAND,
        ).copy(statusMessageKey = "task.paused")

        val bar = projectAssistantBar(ready(errand))

        assertEquals(TaskDisplayState.PAUSED, bar.state)
        assertEquals(3, bar.sourcesRead)
        assertEquals(0, bar.sourcesPlanned)
        assertFalse(bar.countsReadAgainstPlan)
        // No pause of its own to name, so the line is the count of what was
        // read — and with no plan, it is that count alone.
        assertNull(pausedLine(bar))
    }

    @Test
    fun `a comparison counts what it read against the pages it was given`() {
        val compare = task(
            timeline = listOf(
                TaskTimelineEntry(1, TaskTimelineKind.READ_PAGE, "docs.example.test", 2),
            ),
            sources = listOf(source("a", "docs.example.test"), source("b", "shop.example.test")),
            phase = TaskPhase.PAUSED,
            template = TaskTemplate.COMPARE_PRODUCTS,
        )

        val bar = projectAssistantBar(ready(compare))

        assertEquals(1, bar.sourcesRead)
        assertEquals(2, bar.sourcesPlanned)
        assertTrue(bar.countsReadAgainstPlan)
    }

    @Test
    fun `the latest step is the highest sequence whatever the list order`() {
        val first = TaskTimelineEntry(1, TaskTimelineKind.READ_PAGE, "docs.example.test", 3)
        val built = TaskTimelineEntry(3, TaskTimelineKind.BUILT_OUTPUT, count = 1)
        val second = TaskTimelineEntry(2, TaskTimelineKind.READ_PAGE, "shop.example.test", 2)

        val bar = projectAssistantBar(ready(task(timeline = listOf(built, first, second))))

        assertEquals(built, bar.latestStep)
    }

    @Test
    fun `an idle bar that is not set up is still idle and says so`() {
        val idle = projectAssistantBar(ready(task = null), readiness = TaffyReadiness.NotSetUp)
        assertTrue(idle.isIdle)
        assertTrue(idle.invitesSetup)
        val busy = projectAssistantBar(ready(task = task()), readiness = TaffyReadiness.NotSetUp)
        assertFalse(busy.invitesSetup)
        assertFalse(projectAssistantBar(ready(task = null)).invitesSetup)
    }

    @Test
    fun `a failed task names its reason`() {
        val provider = task(phase = TaskPhase.FAILED, failure = CoreFailureCode.PROVIDER_UNAVAILABLE)
        val bar = projectAssistantBar(ready(provider))
        assertEquals(TaskDisplayState.FAILED, bar.state)
        assertEquals(TaskFailureReason.PROVIDER_UNAVAILABLE, bar.failure)
        val other = task(phase = TaskPhase.FAILED, failure = CoreFailureCode.INTERNAL)
        assertEquals(TaskFailureReason.INTERNAL, projectAssistantBar(ready(other)).failure)
        val budget = task(phase = TaskPhase.FAILED, failure = CoreFailureCode.BUDGET_EXCEEDED)
        assertEquals(TaskFailureReason.BUDGET_EXCEEDED, projectAssistantBar(ready(budget)).failure)
        assertNull(projectAssistantBar(ready(task())).failure)
    }

    @Test
    fun `a wait is projected by kind, with the ask's own words`() {
        val asking = task().copy(statusMessageKey = "task.waiting_for_input", askPrompt = "Which one?")
        val bar = projectAssistantBar(ready(task = asking))
        assertTrue(bar.hasAsk)
        assertEquals("Which one?", bar.askPrompt)
        assertFalse(bar.hasInputRequest)

        val form = task().copy(inputRequest = "request-1")
        assertTrue(projectAssistantBar(ready(task = form)).hasInputRequest)
        assertFalse(projectAssistantBar(ready(task = form)).hasAsk)
    }

    private fun ready(task: TaskProjection?) =
        TaskRepositoryState(CoreUiAvailability.READY, 1u, task)

    private fun task(
        timeline: List<TaskTimelineEntry> = emptyList(),
        sources: List<SourceRecord> = emptyList(),
        phase: TaskPhase = TaskPhase.RUNNING,
        failure: CoreFailureCode? = null,
        template: TaskTemplate = TaskTemplate.SUMMARIZE_EVIDENCE,
    ) = TaskProjection(
        id = "task-1",
        revision = 3u,
        phase = phase,
        goal = "Is the fee refundable?",
        template = template,
        progressBasisPoints = 0u,
        statusMessageKey = null,
        failure = failure,
        pendingAction = null,
        timeline = timeline,
        sources = sources,
    )

    private fun source(id: String, host: String) = SourceRecord(
        id = SourceId(id),
        title = host,
        host = host,
        readAtEpochMillis = 0,
        factCount = 0,
    )
}
