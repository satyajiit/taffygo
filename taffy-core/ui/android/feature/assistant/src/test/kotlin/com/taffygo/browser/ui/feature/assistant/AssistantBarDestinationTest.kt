// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

/**
 * Where the pill goes, for a task that has ended.
 *
 * The pill is the only control an ended task has, and every surface it can
 * reach has to be one that says something about *that* task. The workspace
 * list is not: it is the library of research a person has kept, reached from
 * the dock's own Workspaces door, and an errand that keeps nothing leaves it
 * empty — so the tap landed on "No workspaces yet" and the task view holding
 * the answer, the sources and the only way to be done with the task could not
 * be reached from the bar at all (decision 0180).
 */
@OptIn(ExperimentalCoroutinesApi::class)
class AssistantBarDestinationTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun useTestMain() = Dispatchers.setMain(dispatcher)

    @After
    fun restoreMain() = Dispatchers.resetMain()

    @Test
    fun `every door an ended task's pill opens is the task view`() = runTest(dispatcher) {
        for (phase in listOf(
            TaskPhase.COMPLETED,
            TaskPhase.PARTIAL,
            TaskPhase.FAILED,
            TaskPhase.CANCELLED,
        )) {
            val tasks = TestTasks()
            tasks.status.value = tasks.status.value.copy(task = ended(phase))
            val bar = AssistantBarViewModel(tasks, TestReadiness())
            val navigator = RecordingNavigator()

            bar.onIntent(AssistantBarIntent.OpenResults, navigator)
            bar.onIntent(AssistantBarIntent.OpenAssistant, navigator)

            assertEquals(
                "$phase must open its own task view, twice",
                listOf(TaffyDestination.TaskView, TaffyDestination.TaskView),
                navigator.destinations,
            )
        }
    }

    /** The library keeps its own door, and the pill is not it. */
    @Test
    fun `the pill never opens the workspace list`() = runTest(dispatcher) {
        val tasks = TestTasks()
        tasks.status.value = tasks.status.value.copy(task = ended(TaskPhase.PARTIAL))
        val bar = AssistantBarViewModel(tasks, TestReadiness())
        val navigator = RecordingNavigator()

        for (intent in listOf(
            AssistantBarIntent.OpenResults,
            AssistantBarIntent.OpenAssistant,
            AssistantBarIntent.ReviewApproval,
        )) {
            bar.onIntent(intent, navigator)
        }

        assertEquals(emptyList<TaffyDestination>(), navigator.destinations.filter {
            it == TaffyDestination.WorkspaceList
        })
    }

    private fun ended(phase: TaskPhase) = TaskProjection(
        id = "task-1",
        revision = 3u,
        phase = phase,
        goal = "download my eAadhaar",
        template = TaskTemplate.WEB_ERRAND,
        progressBasisPoints = 10_000u,
        statusMessageKey = null,
        failure = null,
        pendingAction = null,
    )
}
