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
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskControlRefusal
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
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

/**
 * What the two surfaces do with a control the browser would not take.
 *
 * A paused task restored after a browser restart still carries Resume — the
 * core owns the control list and the browser owns the authority to honour it,
 * and only the browser knows the consent is gone. The command is answered on
 * admission, so the refusal changed nothing a person could see and both
 * surfaces threw it away (decision 0221).
 */
@OptIn(ExperimentalCoroutinesApi::class)
class TaskControlRefusalTest {
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
    fun `a refused resume is said on the task view and can be put away`() = runTest(dispatcher) {
        val tasks = ControlTasks()
        val viewModel = TaskViewViewModel(tasks, NoAnalytics)
        val collection = collecting(viewModel)

        viewModel.onIntent(TaskViewIntent.Control(TaskControl.RESUME))
        runCurrent()

        assertEquals(listOf(TaskControl.RESUME), tasks.used)
        val refusal = viewModel.state.value.controlRefusal
        assertEquals(TaskControl.RESUME, refusal?.control)
        assertEquals(FailureReason.INVALID_REQUEST, refusal?.reason)

        viewModel.onIntent(TaskViewIntent.DismissControlRefusal)
        runCurrent()
        assertNull(viewModel.state.value.controlRefusal)
        collection.cancelAndJoin()
    }

    /**
     * A control that was taken says nothing, which is the ordinary case and
     * has to stay silent: a notice on every press would be its own defect.
     */
    @Test
    fun `an admitted control says nothing`() = runTest(dispatcher) {
        val tasks = ControlTasks(answer = TaffyResult.Success(Unit))
        val viewModel = TaskViewViewModel(tasks, NoAnalytics)
        val collection = collecting(viewModel)

        viewModel.onIntent(TaskViewIntent.Control(TaskControl.RESUME))
        runCurrent()

        assertEquals(listOf(TaskControl.RESUME), tasks.used)
        assertNull(viewModel.state.value.controlRefusal)
        collection.cancelAndJoin()
    }

    /**
     * The pill has one line and no room for a sentence (decision 0141), so it
     * takes the button away instead of leaving one that has been answered no.
     */
    @Test
    fun `the pill withdraws a control the browser refused`() {
        val offered = AssistantBarUiState(
            taskId = TASK,
            taskRevision = REVISION,
            controls = listOf(TaskControl.RESUME, TaskControl.STOP),
        )

        val after = withdrawRefusedControl(
            offered,
            TaskControlRefusal(TASK, REVISION, TaskControl.RESUME, FailureReason.INVALID_REQUEST),
        )

        assertEquals(listOf(TaskControl.STOP), after.controls)
        assertTrue(TaskControl.RESUME !in after.controls)
    }

    /**
     * Scoped to the revision it was refused at. A later revision is a later
     * question, and withholding a control for ever on one old answer would be
     * the same defect facing the other way.
     */
    @Test
    fun `a refusal at an earlier revision withdraws nothing`() {
        val offered = AssistantBarUiState(
            taskId = TASK,
            taskRevision = REVISION + 1uL,
            controls = listOf(TaskControl.RESUME, TaskControl.STOP),
        )

        val after = withdrawRefusedControl(
            offered,
            TaskControlRefusal(TASK, REVISION, TaskControl.RESUME, FailureReason.INVALID_REQUEST),
        )

        assertEquals(listOf(TaskControl.RESUME, TaskControl.STOP), after.controls)
    }

    private fun kotlinx.coroutines.test.TestScope.collecting(viewModel: TaskViewViewModel) =
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.state.collect { }
        }.also { runCurrent() }

    private class ControlTasks(
        private val answer: TaffyResult<Unit> =
            TaffyResult.Failure(FailureReason.INVALID_REQUEST),
    ) : TaskRepository {
        val status = MutableStateFlow(repository(task()))
        val used = mutableListOf<TaskControl>()

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
        override suspend fun saveWorkspace(workspaceId: String, workspaceRevision: ULong) =
            TaffyResult.Success(Unit)

        override suspend fun discardWorkspace(workspaceId: String, workspaceRevision: ULong) =
            TaffyResult.Success(Unit)

        // The repository is where the refusal is recorded in the product, so
        // the double records it here too: a test that set it from the view
        // model would be asserting a path the product does not take.
        override suspend fun useControl(
            taskId: String,
            taskRevision: ULong,
            control: TaskControl,
        ): TaffyResult<Unit> {
            used += control
            status.value = status.value.copy(
                controlRefusal = when (answer) {
                    is TaffyResult.Success -> null
                    is TaffyResult.Failure ->
                        TaskControlRefusal(taskId, taskRevision, control, answer.reason)
                },
            )
            return answer
        }
    }

    private data object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private companion object {
        const val TASK = "task-paused"
        val REVISION: ULong = 12uL

        fun task() = TaskProjection(
            id = TASK,
            revision = REVISION,
            phase = TaskPhase.PAUSED,
            goal = "download my eAadhaar",
            template = TaskTemplate.WEB_ERRAND,
            progressBasisPoints = 5_000u,
            statusMessageKey = null,
            failure = null,
            pendingAction = null,
            allowedControls = listOf(TaskControl.RESUME, TaskControl.STOP),
        )

        fun repository(task: TaskProjection) =
            TaskRepositoryState(CoreUiAvailability.READY, 1uL, task)
    }
}
