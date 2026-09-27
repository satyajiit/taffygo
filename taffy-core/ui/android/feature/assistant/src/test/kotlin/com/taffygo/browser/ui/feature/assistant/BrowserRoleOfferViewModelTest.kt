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
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskArtifactProjection
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.ui.BrowserRoleOffer
import com.taffygo.browser.ui.core.ui.BrowserRoleOfferResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskArtifactKind
import taffy.core_api.TaskPhase

@OptIn(ExperimentalCoroutinesApi::class)
class BrowserRoleOfferViewModelTest {
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
    fun `confirmed kept sourced output offers the platform sheet once`() = runTest(dispatcher) {
        val tasks = AcceptingTasks(sourcedTask())
        val role = RecordingRoleOffer()
        val viewModel = TaskViewViewModel(
            tasks = tasks,
            analytics = NoAnalytics,
            browserRoleOffer = role,
        )

        viewModel.onIntent(TaskViewIntent.AcceptArtifact(ARTIFACT_ID))
        runCurrent()

        assertEquals(listOf(TASK_ID to ARTIFACT_ID), tasks.accepted)
        assertEquals(1, role.offers)
    }

    private class AcceptingTasks(initial: TaskProjection) : TaskRepository {
        private val status = MutableStateFlow(
            TaskRepositoryState(CoreUiAvailability.READY, 1uL, initial),
        )
        val accepted = mutableListOf<Pair<String, String>>()
        override val state: StateFlow<TaskRepositoryState> = status

        override suspend fun acceptTaskArtifact(
            taskId: String,
            artifactId: String,
        ): TaffyResult<Unit> {
            accepted += taskId to artifactId
            val current = requireNotNull(status.value.task)
            status.value = status.value.copy(
                task = current.copy(
                    revision = current.revision + 1uL,
                    artifacts = current.artifacts.map { artifact ->
                        if (artifact.id == artifactId) artifact.copy(accepted = true) else artifact
                    },
                ),
            )
            return TaffyResult.Success(Unit)
        }

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

    private class RecordingRoleOffer : BrowserRoleOffer {
        var offers = 0

        override fun offerAfterAcceptedOutput(): BrowserRoleOfferResult {
            offers += 1
            return BrowserRoleOfferResult.SHEET_LAUNCHED
        }
    }

    private fun sourcedTask(): TaskProjection {
        val source = SourceRecord(SOURCE_ID, "Evidence", "example.test", 10L, 1)
        val fact = Fact(
            id = FactId("fact-1"),
            field = "price",
            value = "42",
            kind = FactKind.FROM_THE_PAGE,
            sources = listOf(SOURCE_ID),
        )
        return TaskProjection(
            id = TASK_ID,
            revision = 3uL,
            phase = TaskPhase.COMPLETED,
            goal = "compare",
            template = TaskTemplate.COMPARE_PRODUCTS,
            progressBasisPoints = 10_000u,
            statusMessageKey = null,
            failure = null,
            pendingAction = null,
            sources = listOf(source),
            facts = listOf(fact),
            workspaceId = "workspace-1",
            workspaceRevision = 4uL,
            artifacts = listOf(
                TaskArtifactProjection(
                    id = ARTIFACT_ID,
                    kind = TaskArtifactKind.MARKDOWN,
                    workspaceRevision = 4uL,
                    accepted = false,
                ),
            ),
        )
    }

    private data object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private companion object {
        const val TASK_ID = "task-1"
        const val ARTIFACT_ID = "artifact-1"
        val SOURCE_ID = SourceId("source-1")
    }
}
