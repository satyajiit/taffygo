// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.common.di.TaffyProfileIdentity
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.TaskPhase

@OptIn(ExperimentalCoroutinesApi::class)
class TaskContinuationControllerTest {
    @Test
    fun `private profile never registers publishes or controls a task`() = runTest {
        val fixture = fixture(privateProfile = true)

        fixture.controller.start()
        fixture.controller.setWindowVisible("private-window", false)
        advanceTimeBy(500)
        runCurrent()

        assertEquals(0, fixture.registry.registrationCount())
        assertTrue(fixture.platform.publications.isEmpty())
        assertTrue(fixture.tasks.controls.isEmpty())
        fixture.close()
    }

    @Test
    fun `active task becomes foreground only after its profile loses every window`() = runTest {
        val fixture = fixture()
        fixture.controller.start()
        fixture.controller.setWindowVisible("window-a", true)
        runCurrent()

        assertEquals(false, fixture.platform.publications.last().second)

        fixture.controller.setWindowVisible("window-a", false)
        advanceTimeBy(499)
        assertEquals(false, fixture.platform.publications.last().second)
        advanceTimeBy(1)
        runCurrent()

        assertEquals(true, fixture.platform.publications.last().second)
        fixture.close()
    }

    @Test
    fun `unavailable notification pauses exact background revision once`() = runTest {
        val fixture = fixture()
        fixture.platform.available = false
        fixture.controller.start()
        advanceTimeBy(500)
        runCurrent()

        assertEquals(listOf(Control("task", 4u, TaskControl.PAUSE)), fixture.tasks.controls)
        runCurrent()
        assertEquals(1, fixture.tasks.controls.size)

        fixture.tasks.state.value = fixture.tasks.state.value.copy(
            task = fixture.tasks.state.value.task?.copy(revision = 5u),
        )
        runCurrent()
        assertEquals(Control("task", 5u, TaskControl.PAUSE), fixture.tasks.controls.last())
        fixture.close()
    }

    @Test
    fun `disabled notification preference pauses background continuation`() = runTest {
        val fixture = fixture(notificationEnabled = false)

        fixture.controller.start()
        advanceTimeBy(500)
        runCurrent()

        assertEquals(listOf(Control("task", 4u, TaskControl.PAUSE)), fixture.tasks.controls)
        assertTrue(fixture.platform.publications.isEmpty())
        fixture.close()
    }

    @Test
    fun `notification publication failure pauses background continuation`() = runTest {
        val fixture = fixture()
        fixture.platform.publishSucceeds = false

        fixture.controller.start()
        advanceTimeBy(500)
        runCurrent()

        assertEquals(listOf(Control("task", 4u, TaskControl.PAUSE)), fixture.tasks.controls)
        assertEquals(1, fixture.platform.publications.size)
        fixture.close()
    }

    @Test
    fun `notification action is exact stale safe and terminal state removes`() = runTest {
        val fixture = fixture()
        fixture.controller.start()
        fixture.controller.setWindowVisible("window", true)
        runCurrent()
        val projection = fixture.platform.publications.last().first

        assertTrue(
            fixture.registry.dispatch(
                TaskNotificationControlRequest(
                    projection.profileToken,
                    projection.taskId,
                    projection.taskRevision,
                    TaskNotificationAction.STOP,
                ),
            ),
        )
        runCurrent()
        assertEquals(Control("task", 4u, TaskControl.STOP), fixture.tasks.controls.last())
        assertFalse(
            fixture.registry.dispatch(
                TaskNotificationControlRequest(
                    projection.profileToken,
                    projection.taskId,
                    3u,
                    TaskNotificationAction.STOP,
                ),
            ),
        )

        fixture.tasks.state.value = fixture.tasks.state.value.copy(
            task = fixture.tasks.state.value.task?.copy(phase = TaskPhase.COMPLETED),
        )
        runCurrent()
        assertEquals(listOf(projection.notificationId), fixture.platform.removals)
        fixture.close()
        assertEquals(0, fixture.registry.registrationCount())
    }

    private fun TestScope.fixture(
        privateProfile: Boolean = false,
        notificationEnabled: Boolean = true,
    ): Fixture {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val lifetime = TaffyProfileLifetime(
            TestDispatchers(dispatcher),
            CoroutineFailureSink { error("unexpected coroutine failure: $it") },
        )
        val store = PreferenceStore().apply {
            if (notificationEnabled) {
                putString(
                    ProfilePreferenceNames.NOTIFICATION_TOPICS,
                    NotificationTopic.TASK_PROGRESS.name,
                )
            }
        }
        val tasks = Tasks(runningTask())
        val registry = TaskContinuationRegistry(capacity = 2)
        val platform = Platform()
        val controller = TaskContinuationController(
            tasks,
            ProfileUserPreferencesRepository(store),
            TaffyProfileIdentity("profile", private = privateProfile),
            lifetime,
            registry,
            platform,
        )
        return Fixture(controller, tasks, registry, platform, lifetime)
    }

    private fun runningTask() = TaskProjection(
        id = "task",
        revision = 4u,
        phase = TaskPhase.RUNNING,
        goal = "private goal",
        template = TaskTemplate.SUMMARIZE_EVIDENCE,
        progressBasisPoints = 1_500u,
        statusMessageKey = null,
        failure = null,
        pendingAction = null,
        allowedControls = listOf(TaskControl.PAUSE, TaskControl.STOP),
    )

    private data class Fixture(
        val controller: TaskContinuationController,
        val tasks: Tasks,
        val registry: TaskContinuationRegistry,
        val platform: Platform,
        val lifetime: TaffyProfileLifetime,
    ) {
        fun close() = lifetime.close()
    }

    private data class Control(val taskId: String, val revision: ULong, val control: TaskControl)

    private class Platform : TaskContinuationPlatform {
        var available = true
        var publishSucceeds = true
        val publications = mutableListOf<Pair<TaskNotificationProjection, Boolean>>()
        val removals = mutableListOf<Int>()

        override fun canPostNotifications(): Boolean = available

        override fun publish(
            projection: TaskNotificationProjection,
            requireForeground: Boolean,
        ): Boolean {
            publications += projection to requireForeground
            return available && publishSucceeds
        }

        override fun remove(profileToken: String, notificationId: Int) {
            removals += notificationId
        }
    }

    private class TestDispatchers(
        override val default: CoroutineDispatcher,
    ) : AppDispatchers {
        override val main = default
        override val io = default
    }

    private class PreferenceStore : ProfilePreferenceStore {
        private val strings = mutableMapOf<String, String>()
        override fun getString(name: String): String = strings[name].orEmpty()
        override fun putString(name: String, value: String) {
            strings[name] = value
        }
        override fun getBoolean(name: String): Boolean = false
        override fun putBoolean(name: String, value: Boolean) = Unit
    }

    private class Tasks(task: TaskProjection) : TaskRepository {
        override val state = MutableStateFlow(
            TaskRepositoryState(CoreUiAvailability.READY, 1u, task),
        )
        val controls = mutableListOf<Control>()

        override suspend fun startTask(
            goal: String,
            template: TaskTemplate,
            consent: TaskConsentIntent,
            workspaceId: String?,
        ) = TaffyResult.Success(Unit)

        override suspend fun cancelTask(taskId: String) = TaffyResult.Success(Unit)

        override suspend fun useControl(
            taskId: String,
            taskRevision: ULong,
            control: TaskControl,
        ): TaffyResult<Unit> {
            val current = state.value.task
            if (current?.id != taskId || current.revision != taskRevision ||
                control !in current.allowedControls
            ) {
                return TaffyResult.Failure(
                    com.taffygo.browser.ui.core.common.FailureReason.STALE_REVISION,
                )
            }
            controls += Control(taskId, taskRevision, control)
            return TaffyResult.Success(Unit)
        }

        override suspend fun completeHandover(taskId: String) = TaffyResult.Success(Unit)
        override suspend fun supplyUserInput(taskId: String, answer: String) =
            TaffyResult.Success(Unit)
        override suspend fun approveAction(taskId: String, actionId: String) =
            TaffyResult.Success(Unit)
        override suspend fun retryCore() = TaffyResult.Success(Unit)
    }
}
