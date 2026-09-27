// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

@OptIn(ExperimentalCoroutinesApi::class)
class TaskOutputProjectionTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `task screen keeps repository timeline sources and facts`() {
        val source = SourceRecord(SourceId("source-1"), "Evidence", "example.test", 10, 1)
        val fact = Fact(
            id = FactId("fact-1"),
            field = "price",
            value = "42",
            kind = FactKind.FROM_THE_PAGE,
            sources = listOf(source.id),
        )
        val timeline = TaskTimelineEntry(0, TaskTimelineKind.READ_PAGE, source.host, 1, 10)
        val answer = TaskAnswerProjection(
            segments = listOf("Visible answer"),
            isStreaming = true,
            isIncomplete = false,
            isTruncated = false,
        )
        val task = TaskProjection(
            id = "task-1",
            revision = 3u,
            phase = TaskPhase.RUNNING,
            goal = "compare",
            template = com.taffygo.browser.ui.core.model.TaskTemplate.COMPARE_PRODUCTS,
            progressBasisPoints = 500u,
            statusMessageKey = "task.acting",
            failure = null,
            pendingAction = null,
            timeline = listOf(timeline),
            sources = listOf(source),
            facts = listOf(fact),
            workspaceId = "workspace-1",
            workspaceRevision = 4u,
            canSaveWorkspace = true,
            canDiscardWorkspace = true,
            liveAnswer = answer,
        )

        val projected = projectTaskView(TaskRepositoryState(CoreUiAvailability.READY, 1u, task))

        assertEquals(listOf(timeline), projected.timeline)
        assertEquals(listOf(source), projected.sources)
        assertEquals(listOf(fact), projected.facts)
        assertEquals(answer, projected.liveAnswer)
        assertEquals(true, projected.canSaveWorkspace)
        assertEquals(true, projected.canDiscardWorkspace)
        assertEquals(false, projected.workspaceSaved)
    }

    @Test
    fun `discard confirmation cannot move to a different workspace revision`() =
        runTest(dispatcher) {
            val tasks = RecordingTasks(task(workspaceId = "workspace-1", workspaceRevision = 4uL))
            val viewModel = TaskViewViewModel(tasks, NoAnalytics)
            val collection = backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
                viewModel.state.collect { }
            }
            runCurrent()

            viewModel.onIntent(TaskViewIntent.RequestDiscardWorkspace)
            runCurrent()
            assertTrue(viewModel.state.value.showDiscardConfirmation)

            tasks.status.value = TaskRepositoryState(
                CoreUiAvailability.READY,
                1uL,
                task(workspaceId = "workspace-2", workspaceRevision = 8uL),
            )
            runCurrent()
            assertFalse(viewModel.state.value.showDiscardConfirmation)

            viewModel.onIntent(TaskViewIntent.ConfirmDiscardWorkspace)
            runCurrent()
            assertTrue(tasks.discards.isEmpty())

            tasks.status.value = TaskRepositoryState(
                CoreUiAvailability.READY,
                1uL,
                task(workspaceId = "workspace-1", workspaceRevision = 4uL),
            )
            runCurrent()
            viewModel.onIntent(TaskViewIntent.RequestDiscardWorkspace)
            runCurrent()
            viewModel.onIntent(TaskViewIntent.ConfirmDiscardWorkspace)
            runCurrent()
            assertEquals(listOf("workspace-1" to 4uL), tasks.discards)
            collection.cancelAndJoin()
        }

    private fun task(workspaceId: String, workspaceRevision: ULong) = TaskProjection(
        id = "task-1",
        revision = 3uL,
        phase = TaskPhase.COMPLETED,
        goal = "compare",
        template = com.taffygo.browser.ui.core.model.TaskTemplate.COMPARE_PRODUCTS,
        progressBasisPoints = 10_000u,
        statusMessageKey = null,
        failure = null,
        pendingAction = null,
        workspaceId = workspaceId,
        workspaceRevision = workspaceRevision,
        canDiscardWorkspace = true,
    )

    private class RecordingTasks(initial: TaskProjection) : TaskRepository {
        val status = MutableStateFlow(TaskRepositoryState(CoreUiAvailability.READY, 1uL, initial))
        val discards = mutableListOf<Pair<String, ULong>>()
        override val state: StateFlow<TaskRepositoryState> = status

        override suspend fun startTask(
            goal: String,
            template: com.taffygo.browser.ui.core.model.TaskTemplate,
            consent: TaskConsentIntent,
            workspaceId: String?,
        ) = TaffyResult.Success(Unit)

        override suspend fun cancelTask(taskId: String) = TaffyResult.Success(Unit)
        override suspend fun completeHandover(taskId: String) = TaffyResult.Success(Unit)
        override suspend fun supplyUserInput(taskId: String, answer: String) =
            TaffyResult.Success(Unit)
        override suspend fun approveAction(taskId: String, actionId: String) =
            TaffyResult.Success(Unit)

        override suspend fun discardWorkspace(
            workspaceId: String,
            workspaceRevision: ULong,
        ): TaffyResult<Unit> {
            discards += workspaceId to workspaceRevision
            return TaffyResult.Success(Unit)
        }

        override suspend fun retryCore() = TaffyResult.Success(Unit)
    }

    private data object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}
