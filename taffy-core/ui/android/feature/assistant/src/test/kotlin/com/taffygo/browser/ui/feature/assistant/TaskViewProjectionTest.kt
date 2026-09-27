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
import taffy.core_api.TaskPhase
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CoreFailureCode

/** The failure reason a task view carries, and the one offer it makes. */
class TaskViewProjectionTest {
    @Test
    fun `a provider that could not be reached is its own reason`() {
        val view = projectTaskView(ready(task(TaskPhase.FAILED, CoreFailureCode.PROVIDER_UNAVAILABLE)))
        assertEquals(TaskDisplayState.FAILED, view.state)
        assertEquals(TaskFailureReason.PROVIDER_UNAVAILABLE, view.failure)
        assertTrue(view.offersSetup)
    }

    @Test
    fun `a refused key is its own reason and offers set-up too`() {
        val view = projectTaskView(ready(task(TaskPhase.FAILED, CoreFailureCode.PROVIDER_REFUSED)))
        assertEquals(TaskFailureReason.PROVIDER_REFUSED, view.failure)
        assertTrue(view.offersSetup)
    }

    @Test
    fun `every code has a reason and only the two provider ones offer set-up`() {
        for (code in CoreFailureCode.entries) {
            val view = projectTaskView(ready(task(TaskPhase.FAILED, code)))
            val expected = when (code) {
                CoreFailureCode.PROVIDER_UNAVAILABLE -> TaskFailureReason.PROVIDER_UNAVAILABLE
                CoreFailureCode.PROVIDER_REFUSED -> TaskFailureReason.PROVIDER_REFUSED
                CoreFailureCode.PROVIDER_LIMIT -> TaskFailureReason.PROVIDER_LIMIT
                CoreFailureCode.OFFLINE -> TaskFailureReason.OFFLINE
                CoreFailureCode.BUDGET_EXCEEDED -> TaskFailureReason.BUDGET_EXCEEDED
                CoreFailureCode.DEADLINE_EXCEEDED -> TaskFailureReason.DEADLINE_EXCEEDED
                CoreFailureCode.POLICY_REFUSED,
                CoreFailureCode.POLICY_DENIED,
                -> TaskFailureReason.POLICY_REFUSED
                CoreFailureCode.UNVERIFIABLE_ACTION -> TaskFailureReason.UNVERIFIABLE_ACTION
                CoreFailureCode.OUTCOME_UNKNOWN -> TaskFailureReason.OUTCOME_UNKNOWN
                CoreFailureCode.SOURCES_UNAVAILABLE -> TaskFailureReason.SOURCES_UNAVAILABLE
                else -> TaskFailureReason.INTERNAL
            }
            assertEquals(code.name, expected, view.failure)
            assertEquals(code.name, expected.offersSetup, view.offersSetup)
        }
    }

    @Test
    fun `no failure stays none`() {
        assertNull(projectTaskView(ready(task(TaskPhase.RUNNING, null))).failure)
        assertNull(projectTaskView(ready(null)).failure)
        assertFalse(projectTaskView(ready(null)).offersSetup)
    }

    @Test
    fun `the offer needs the failed state and survives until Not now`() {
        val running = projectTaskView(ready(task(TaskPhase.RUNNING, CoreFailureCode.PROVIDER_UNAVAILABLE)))
        assertFalse(running.offersSetup)
        val failed = projectTaskView(ready(task(TaskPhase.FAILED, CoreFailureCode.PROVIDER_UNAVAILABLE)))
        val dismissed = reduceTaskView(failed, TaskViewIntent.DismissSetup)
        assertTrue(dismissed.setupDismissed)
        assertFalse(dismissed.offersSetup)
        assertEquals(TaskFailureReason.PROVIDER_UNAVAILABLE, dismissed.failure)
    }

    /**
     * The failed task's "Pages read" tile counted the workspace's sources,
     * which are the pages consented to at the start whether or not any was
     * read. It now counts what the task did, the way the bar does, so the two
     * numbers a person sees for one task agree.
     */
    @Test
    fun `a failed task counts the pages it read and not the pages it was given`() {
        val given = listOf("a" to "docs.example.test", "b" to "shop.example.test")
            .map { (id, host) -> SourceRecord(SourceId(id), host, host, 0, 0) }
        val nothingRead = task(TaskPhase.FAILED, CoreFailureCode.SOURCES_UNAVAILABLE)
            .copy(sources = given)
        assertEquals(0, projectTaskView(ready(nothingRead)).pagesRead)

        val errand = task(TaskPhase.FAILED, CoreFailureCode.POLICY_REFUSED).copy(
            template = TaskTemplate.WEB_ERRAND,
            sources = given.take(1),
            timeline = listOf("docs.example.test", "www.google.com", "shop.example.test")
                .mapIndexed { index, host ->
                    TaskTimelineEntry(index.toLong() + 1, TaskTimelineKind.READ_PAGE, host, 0)
                },
        )
        val view = projectTaskView(ready(errand))
        assertEquals(3, view.pagesRead)
        assertEquals(projectAssistantBar(ready(errand)).sourcesRead, view.pagesRead)
    }

    private fun ready(task: TaskProjection?) =
        TaskRepositoryState(CoreUiAvailability.READY, 1u, task)

    private fun task(phase: TaskPhase, failure: CoreFailureCode?) = TaskProjection(
        id = "task-1",
        revision = 3u,
        phase = phase,
        goal = "compare these two policies",
        template = TaskTemplate.SUMMARIZE_EVIDENCE,
        progressBasisPoints = 0u,
        statusMessageKey = null,
        failure = failure,
        pendingAction = null,
    )
}
