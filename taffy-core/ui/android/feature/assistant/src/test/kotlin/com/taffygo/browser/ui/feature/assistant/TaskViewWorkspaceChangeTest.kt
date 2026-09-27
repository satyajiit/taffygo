// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
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
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

/**
 * What the screen says about a save or a discard.
 *
 * The command is answered on admission, not on completion, so there are three
 * distinct things a person can be owed and the code used to say none of them:
 * the answer was read and thrown away, and the dialog closed either way.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class TaskViewWorkspaceChangeTest {
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
    fun `a refused discard says so and can be put away`() = runTest(dispatcher) {
        val tasks = WorkspaceTasks(answer = TaffyResult.Failure(FailureReason.NOT_PERMITTED))
        val viewModel = TaskViewViewModel(tasks, NoAnalytics)
        val collection = collecting(viewModel)

        viewModel.onIntent(TaskViewIntent.RequestDiscardWorkspace)
        runCurrent()
        assertTrue(viewModel.state.value.showDiscardConfirmation)

        viewModel.onIntent(TaskViewIntent.ConfirmDiscardWorkspace)
        runCurrent()

        assertEquals(listOf(WORKSPACE to REVISION), tasks.discarded)
        assertFalse(viewModel.state.value.showDiscardConfirmation)
        assertEquals(FailureReason.NOT_PERMITTED, viewModel.state.value.workspaceChangeRefusal)
        assertFalse(viewModel.state.value.workspaceChangeInFlight)

        viewModel.onIntent(TaskViewIntent.DismissWorkspaceChangeRefusal)
        runCurrent()
        assertNull(viewModel.state.value.workspaceChangeRefusal)
        collection.cancelAndJoin()
    }

    @Test
    fun `a duplicate keeps its own reason, because it means wait`() = runTest(dispatcher) {
        val tasks = WorkspaceTasks(answer = TaffyResult.Failure(FailureReason.DUPLICATE))
        val viewModel = TaskViewViewModel(tasks, NoAnalytics)
        val collection = collecting(viewModel)

        viewModel.onIntent(TaskViewIntent.SaveWorkspace)
        runCurrent()

        assertEquals(listOf(WORKSPACE to REVISION), tasks.saved)
        assertEquals(FailureReason.DUPLICATE, viewModel.state.value.workspaceChangeRefusal)
        collection.cancelAndJoin()
    }

    @Test
    fun `an admitted discard is in flight until the workspace goes`() = runTest(dispatcher) {
        val tasks = WorkspaceTasks(answer = TaffyResult.Success(Unit))
        val viewModel = TaskViewViewModel(tasks, NoAnalytics)
        val collection = collecting(viewModel)

        viewModel.onIntent(TaskViewIntent.RequestDiscardWorkspace)
        runCurrent()
        viewModel.onIntent(TaskViewIntent.ConfirmDiscardWorkspace)
        runCurrent()

        // Admission is not completion: the projection still offers the
        // workspace, so the person is told the change is running.
        assertNull(viewModel.state.value.workspaceChangeRefusal)
        assertTrue(viewModel.state.value.workspaceChangeInFlight)

        tasks.status.value = repository(task(workspaceId = null))
        runCurrent()
        assertFalse(viewModel.state.value.workspaceChangeInFlight)
        collection.cancelAndJoin()
    }

    @Test
    fun `a discard that no longer matches its confirmation is never sent`() = runTest(dispatcher) {
        val tasks = WorkspaceTasks(answer = TaffyResult.Success(Unit))
        val viewModel = TaskViewViewModel(tasks, NoAnalytics)
        val collection = collecting(viewModel)

        viewModel.onIntent(TaskViewIntent.RequestDiscardWorkspace)
        runCurrent()
        tasks.status.value = repository(task(revision = REVISION + 1uL))
        runCurrent()

        viewModel.onIntent(TaskViewIntent.ConfirmDiscardWorkspace)
        runCurrent()

        assertEquals(emptyList<Pair<String, ULong>>(), tasks.discarded)
        assertNull(viewModel.state.value.workspaceChangeRefusal)
        assertFalse(viewModel.state.value.workspaceChangeInFlight)
        collection.cancelAndJoin()
    }

    private fun kotlinx.coroutines.test.TestScope.collecting(viewModel: TaskViewViewModel) =
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.state.collect { }
        }.also { runCurrent() }

    private class WorkspaceTasks(private val answer: TaffyResult<Unit>) : TaskRepository {
        val status = MutableStateFlow(repository(task()))
        val saved = mutableListOf<Pair<String, ULong>>()
        val discarded = mutableListOf<Pair<String, ULong>>()

        override val state: StateFlow<TaskRepositoryState> = status

        override suspend fun startTask(
            goal: String,
            template: TaskTemplate,
            consent: TaskConsentIntent,
            workspaceId: String?,
        ) = TaffyResult.Success(Unit)

        override suspend fun cancelTask(taskId: String) = TaffyResult.Success(Unit)
        override suspend fun completeHandover(taskId: String) = TaffyResult.Success(Unit)
        override suspend fun supplyUserInput(taskId: String, answer: String) =
            TaffyResult.Success(Unit)
        override suspend fun approveAction(taskId: String, actionId: String) =
            TaffyResult.Success(Unit)
        override suspend fun retryCore() = TaffyResult.Success(Unit)

        override suspend fun saveWorkspace(
            workspaceId: String,
            workspaceRevision: ULong,
        ): TaffyResult<Unit> {
            saved += workspaceId to workspaceRevision
            return answer
        }

        override suspend fun discardWorkspace(
            workspaceId: String,
            workspaceRevision: ULong,
        ): TaffyResult<Unit> {
            discarded += workspaceId to workspaceRevision
            return answer
        }
    }

    private data object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private companion object {
        const val WORKSPACE = "workspace-1"
        val REVISION: ULong = 7uL

        fun task(
            workspaceId: String? = WORKSPACE,
            revision: ULong = REVISION,
        ) = TaskProjection(
            id = "task-workspace",
            revision = 3uL,
            phase = TaskPhase.COMPLETED,
            goal = "download my eAadhaar",
            template = TaskTemplate.SUMMARIZE_EVIDENCE,
            progressBasisPoints = 10_000u,
            statusMessageKey = null,
            failure = null,
            pendingAction = null,
            workspaceId = workspaceId,
            workspaceRevision = workspaceId?.let { revision },
            canSaveWorkspace = workspaceId != null,
            canDiscardWorkspace = workspaceId != null,
        )

        fun repository(task: TaskProjection) =
            TaskRepositoryState(CoreUiAvailability.READY, 1uL, task)
    }
}
