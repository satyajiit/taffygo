// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.model.TaskInputRequest
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskInputRepository
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
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

/** Take over gives the page back, and does nothing at all when nothing has it. */
@OptIn(ExperimentalCoroutinesApi::class)
class BrowserTakeoverViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `taking over sends the dedicated control without stopping the task`() =
        runTest(dispatcher) {
            val tasks = RecordingTasks(runningTask())
            val viewModel = BrowserTakeoverViewModel(tasks, NoForms(), FakeBrowser())
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.takeOver()
            runCurrent()

            assertEquals("task-1" to TaskControl.TAKE_OVER, tasks.control)
            assertNull(tasks.cancelled)
        }

    @Test
    fun `taking over with nothing running sends no command`() = runTest(dispatcher) {
        val tasks = RecordingTasks(null)
        val viewModel = BrowserTakeoverViewModel(tasks, NoForms(), FakeBrowser())
        runCurrent()

        viewModel.takeOver()
        runCurrent()

        assertNull(tasks.control)
    }

    @Test
    fun `a stale tap sends no command after take over is withdrawn`() = runTest(dispatcher) {
        val tasks = RecordingTasks(runningTask(controls = emptyList()))
        val viewModel = BrowserTakeoverViewModel(tasks, NoForms(), FakeBrowser())

        viewModel.takeOver()
        runCurrent()

        assertNull(tasks.control)
    }

    @Test
    fun `a tap rendered for an older revision cannot control the newer revision`() =
        runTest(dispatcher) {
            val tasks = RecordingTasks(runningTask())
            val viewModel = BrowserTakeoverViewModel(tasks, NoForms(), FakeBrowser())
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            tasks.replace(runningTask(revision = 2uL))
            viewModel.takeOver()
            runCurrent()

            assertNull(tasks.control)
        }

    @Test
    fun `a task under way claims the page in front of the person`() = runTest(dispatcher) {
        val viewModel = BrowserTakeoverViewModel(
            RecordingTasks(runningTask()),
            NoForms(),
            FakeBrowser(host = "portal.example.test"),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        assertTrue(viewModel.state.value.active)
        assertTrue(viewModel.state.value.hasTask)
        assertFalse(viewModel.state.value.taskEnded)
    }

    private fun runningTask(
        revision: ULong = 1uL,
        controls: List<TaskControl> = listOf(TaskControl.TAKE_OVER),
    ) = TaskProjection(
        id = "task-1",
        revision = revision,
        phase = TaskPhase.RUNNING,
        goal = "get the document",
        template = TaskTemplate.WEB_ERRAND,
        progressBasisPoints = 0u,
        statusMessageKey = null,
        failure = null,
        pendingAction = null,
        allowedControls = controls,
    )

    private class RecordingTasks(task: TaskProjection?) : TaskRepository {
        var control: Pair<String, TaskControl>? = null
            private set
        var cancelled: String? = null
            private set

        private val mutableState =
            MutableStateFlow(TaskRepositoryState(CoreUiAvailability.READY, 1u, task))
        override val state: StateFlow<TaskRepositoryState> = mutableState

        fun replace(task: TaskProjection) {
            mutableState.value = mutableState.value.copy(task = task)
        }

        override suspend fun startTask(
            goal: String,
            template: TaskTemplate,
            consent: TaskConsentIntent,
            workspaceId: String?,
        ) = TaffyResult.Success(Unit)

        override suspend fun cancelTask(taskId: String): TaffyResult<Unit> {
            cancelled = taskId
            return TaffyResult.Success(Unit)
        }

        override suspend fun useControl(
            taskId: String,
            taskRevision: ULong,
            control: TaskControl,
        ): TaffyResult<Unit> {
            val current = state.value.task
            if (current?.id != taskId || current.revision != taskRevision ||
                control !in current.allowedControls
            ) {
                return TaffyResult.Failure(FailureReason.STALE_REVISION)
            }
            this.control = taskId to control
            return TaffyResult.Success(Unit)
        }

        override suspend fun completeHandover(taskId: String) = TaffyResult.Success(Unit)

        override suspend fun supplyUserInput(taskId: String, answer: String) =
            TaffyResult.Success(Unit)

        override suspend fun approveAction(taskId: String, actionId: String) =
            TaffyResult.Success(Unit)

        override suspend fun retryCore() = TaffyResult.Success(Unit)
    }

    private class NoForms : TaskInputRepository {
        override val isAvailable: Boolean = false

        override val request: StateFlow<TaskInputRequest?> = MutableStateFlow(null)

        override suspend fun submit(requestId: String, values: Map<String, String>) =
            TaffyResult.Success(Unit)

        override suspend fun completeInteractive(requestId: String) = TaffyResult.Success(Unit)
    }
}
