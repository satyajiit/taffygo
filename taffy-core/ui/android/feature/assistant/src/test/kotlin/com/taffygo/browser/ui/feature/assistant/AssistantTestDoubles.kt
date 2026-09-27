// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

// The doubles this feature's view-model tests share. One file, because a
// second copy of a double is a second thing to keep true.

internal class TestTasks : TaskRepository {
    val status = MutableStateFlow(TaskRepositoryState(CoreUiAvailability.READY, 1u, null))
    val starts = mutableListOf<TaskConsentIntent>()
    val skillOfferIds = mutableListOf<String?>()
    val handedBack = mutableListOf<String>()
    val answered = mutableListOf<Pair<String, String>>()
    var startBlock: suspend () -> TaffyResult<Unit> = { TaffyResult.Success(Unit) }

    override val state: StateFlow<TaskRepositoryState> = status

    override suspend fun startTask(
        goal: String,
        template: TaskTemplate,
        consent: TaskConsentIntent,
        workspaceId: String?,
    ): TaffyResult<Unit> {
        starts += consent
        return startBlock()
    }

    override suspend fun startTask(
        goal: String,
        template: TaskTemplate,
        consent: TaskConsentIntent,
        workspaceId: String?,
        skillOfferId: String?,
    ): TaffyResult<Unit> {
        skillOfferIds += skillOfferId
        return startTask(goal, template, consent, workspaceId)
    }

    override suspend fun cancelTask(taskId: String) = TaffyResult.Success(Unit)
    override suspend fun completeHandover(taskId: String): TaffyResult<Unit> {
        handedBack += taskId
        return TaffyResult.Success(Unit)
    }
    override suspend fun supplyUserInput(taskId: String, answer: String): TaffyResult<Unit> {
        answered += taskId to answer
        return TaffyResult.Success(Unit)
    }
    override suspend fun approveAction(taskId: String, actionId: String) =
        TaffyResult.Success(Unit)
    override suspend fun retryCore() = TaffyResult.Success(Unit)
}

internal class TestReadiness(
    verdict: TaffyReadiness = TaffyReadiness.NotSetUp,
) : TaffyReadinessRepository {
    val verdicts = MutableStateFlow(verdict)
    override val readiness: StateFlow<TaffyReadiness> = verdicts
}

internal class RecordingNavigator : TaffyNavigator {
    val destinations = mutableListOf<TaffyDestination>()
    var backCount = 0

    override fun goTo(destination: TaffyDestination) {
        destinations += destination
    }

    override fun replaceCurrent(destination: TaffyDestination) = Unit
    override fun goBack(): Boolean {
        backCount += 1
        return true
    }

    override fun goHome() = Unit
    override fun restart(destination: TaffyDestination) = Unit
    override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
}

internal class NoAnalytics : AnalyticsClient {
    override fun record(event: AnalyticsEvent) = Unit
    override fun recent(): List<AnalyticsEvent> = emptyList()
}
