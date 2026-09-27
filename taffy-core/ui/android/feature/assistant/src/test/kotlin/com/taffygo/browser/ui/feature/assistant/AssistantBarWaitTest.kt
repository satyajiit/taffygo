// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskProjection
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

/** The two doors the wait card closes through, and when they are shut. */
@OptIn(ExperimentalCoroutinesApi::class)
class AssistantBarWaitTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun useTestMain() = Dispatchers.setMain(dispatcher)

    @After
    fun restoreMain() = Dispatchers.resetMain()

    @Test
    fun `hand back completes the followed task's hand-over, and only while it has one`() =
        runTest(dispatcher) {
            val tasks = TestTasks()
            tasks.status.value = tasks.status.value.copy(task = waiting("task.waiting_for_handover"))
            val bar = AssistantBarViewModel(tasks, TestReadiness())

            bar.onIntent(AssistantBarIntent.CompleteHandover, RecordingNavigator())
            runCurrent()
            assertEquals(listOf("task-1"), tasks.handedBack)

            // The wait has moved on: a late tap completes nothing twice.
            tasks.status.value = tasks.status.value.copy(task = waiting("task.running"))
            bar.onIntent(AssistantBarIntent.CompleteHandover, RecordingNavigator())
            runCurrent()
            assertEquals(listOf("task-1"), tasks.handedBack)
        }

    @Test
    fun `an answer reaches the followed task only while it is asking`() =
        runTest(dispatcher) {
            val tasks = TestTasks()
            tasks.status.value = tasks.status.value.copy(task = waiting("task.running"))
            val bar = AssistantBarViewModel(tasks, TestReadiness())

            bar.onIntent(AssistantBarIntent.Answer("the blue one"), RecordingNavigator())
            runCurrent()
            assertTrue(tasks.answered.isEmpty())

            tasks.status.value = tasks.status.value.copy(task = waiting("task.waiting_for_input"))
            bar.onIntent(AssistantBarIntent.Answer("the blue one"), RecordingNavigator())
            runCurrent()
            assertEquals(listOf("task-1" to "the blue one"), tasks.answered)
        }

    private fun waiting(statusMessageKey: String) = TaskProjection(
        id = "task-1",
        revision = 3u,
        phase = TaskPhase.WAITING_FOR_USER,
        goal = "download my aadhaar",
        template = TaskTemplate.WEB_ERRAND,
        progressBasisPoints = 0u,
        statusMessageKey = statusMessageKey,
        failure = null,
        pendingAction = null,
    )
}
