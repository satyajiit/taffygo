// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModelStore
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.ui.ReadAloud
import com.taffygo.browser.ui.core.ui.ReadAloudEvent
import com.taffygo.browser.ui.core.ui.ReadAloudSession
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

@OptIn(ExperimentalCoroutinesApi::class)
class TaskViewReadAloudTest {
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
    fun `only a final visible answer is eligible`() {
        val final = projectTaskView(repository(task(answer(streaming = false))))
        val streaming = projectTaskView(repository(task(answer(streaming = true))))
        val running = projectTaskView(
            repository(task(answer(streaming = false), phase = TaskPhase.RUNNING)),
        )

        assertTrue(final.canReadAnswerAloud)
        assertFalse(streaming.canReadAnswerAloud)
        assertFalse(running.canReadAnswerAloud)
    }

    @Test
    fun `person start speaks exactly the visible final segments and stop cancels`() =
        runTest(dispatcher) {
            val tasks = RecordingTasks(task(answer(streaming = false)))
            val speech = RecordingReadAloud()
            val viewModel = TaskViewViewModel(tasks, NoAnalytics, speech)
            val collection = backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
                viewModel.state.collect { }
            }
            runCurrent()

            assertNull(speech.text)
            viewModel.onIntent(TaskViewIntent.ReadAnswerAloud)
            runCurrent()

            assertEquals("First part and second.", speech.text)
            assertEquals(ReadAloudUiState.Preparing, viewModel.state.value.readAloud)

            speech.emit(ReadAloudEvent.Speaking)
            runCurrent()
            assertEquals(ReadAloudUiState.Speaking, viewModel.state.value.readAloud)

            viewModel.onIntent(TaskViewIntent.StopReadAloud)
            runCurrent()
            assertEquals(1, speech.closeCount)
            assertEquals(ReadAloudUiState.Idle, viewModel.state.value.readAloud)
            collection.cancelAndJoin()
        }

    @Test
    fun `streaming answer never reaches the platform`() = runTest(dispatcher) {
        val speech = RecordingReadAloud()
        val viewModel = TaskViewViewModel(
            RecordingTasks(task(answer(streaming = true))),
            NoAnalytics,
            speech,
        )

        viewModel.onIntent(TaskViewIntent.ReadAnswerAloud)

        assertNull(speech.text)
    }

    @Test
    fun `platform failure becomes visible and never retries by itself`() = runTest(dispatcher) {
        val speech = RecordingReadAloud()
        val viewModel = TaskViewViewModel(
            RecordingTasks(task(answer(streaming = false))),
            NoAnalytics,
            speech,
        )
        val collection = backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.state.collect { }
        }
        runCurrent()

        viewModel.onIntent(TaskViewIntent.ReadAnswerAloud)
        speech.emit(ReadAloudEvent.Failed)
        runCurrent()

        assertEquals(ReadAloudUiState.Error, viewModel.state.value.readAloud)
        assertEquals("First part and second.", speech.text)
        collection.cancelAndJoin()
    }

    @Test
    fun `answer change and view model destruction each close active playback`() =
        runTest(dispatcher) {
            val tasks = RecordingTasks(task(answer(streaming = false)))
            val speech = RecordingReadAloud()
            val viewModel = TaskViewViewModel(tasks, NoAnalytics, speech)
            val collection = backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
                viewModel.state.collect { }
            }
            runCurrent()

            viewModel.onIntent(TaskViewIntent.ReadAnswerAloud)
            tasks.status.value = repository(
                task(
                    TaskAnswerProjection(
                        segments = listOf("A replacement answer."),
                        isStreaming = false,
                        isIncomplete = false,
                        isTruncated = false,
                    ),
                ),
            )
            runCurrent()
            assertEquals(1, speech.closeCount)

            viewModel.onIntent(TaskViewIntent.ReadAnswerAloud)
            val store = ViewModelStore()
            store.put("task", viewModel)
            store.clear()
            assertEquals(2, speech.closeCount)
            collection.cancelAndJoin()
        }

    private fun answer(streaming: Boolean) = TaskAnswerProjection(
        segments = listOf("First part", " and second."),
        isStreaming = streaming,
        isIncomplete = false,
        isTruncated = false,
    )

    private fun task(
        answer: TaskAnswerProjection,
        phase: TaskPhase = TaskPhase.COMPLETED,
    ) = TaskProjection(
        id = "task-voice",
        revision = 2uL,
        phase = phase,
        goal = "answer me",
        template = TaskTemplate.SUMMARIZE_EVIDENCE,
        progressBasisPoints = if (phase == TaskPhase.COMPLETED) 10_000u else 5_000u,
        statusMessageKey = null,
        failure = null,
        pendingAction = null,
        liveAnswer = answer,
    )

    private fun repository(task: TaskProjection) =
        TaskRepositoryState(CoreUiAvailability.READY, 1uL, task)

    private class RecordingReadAloud : ReadAloud {
        var text: String? = null
        var closeCount = 0
        private var onEvent: ((ReadAloudEvent) -> Unit)? = null

        override fun speak(
            text: String,
            onEvent: (ReadAloudEvent) -> Unit,
        ): ReadAloudSession {
            this.text = text
            this.onEvent = onEvent
            return ReadAloudSession { closeCount++ }
        }

        fun emit(event: ReadAloudEvent) {
            onEvent?.invoke(event)
        }
    }

    private class RecordingTasks(initial: TaskProjection) : TaskRepository {
        val status = MutableStateFlow(
            TaskRepositoryState(CoreUiAvailability.READY, 1uL, initial),
        )
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
    }

    private data object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}
