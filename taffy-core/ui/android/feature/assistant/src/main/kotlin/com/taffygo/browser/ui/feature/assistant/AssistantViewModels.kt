// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import com.taffygo.browser.ui.core.task.TaskInputRepository
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.ui.BrowserRoleOffer
import com.taffygo.browser.ui.core.ui.ReadAloud
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by the assistant feature. */
@Module
object AssistantViewModels {
    @Provides
    @IntoMap
    @ViewModelKey(TaskDownloadsViewModel::class)
    fun taskDownloads(browser: BrowserRepository, tasks: TaskRepository): ViewModelCreator =
        ViewModelCreator { TaskDownloadsViewModel(browser, tasks) }

    @Provides
    @IntoMap
    @ViewModelKey(TaskSkillReviewViewModel::class)
    fun taskSkillReview(core: CoreApiClient, tasks: TaskRepository, reviews: SavedFlowReviewRepository): ViewModelCreator =
        ViewModelCreator { TaskSkillReviewViewModel(core, tasks, reviews) }

    @Provides
    @IntoMap
    @ViewModelKey(TaskBrowserViewModel::class)
    fun taskBrowser(
        browser: BrowserRepository,
        tasks: TaskRepository,
    ): ViewModelCreator = ViewModelCreator {
        TaskBrowserViewModel(browser, tasks)
    }

    @Provides
    @IntoMap
    @ViewModelKey(AssistantBarViewModel::class)
    fun assistantBar(
        tasks: TaskRepository,
        readiness: TaffyReadinessRepository,
    ): ViewModelCreator = ViewModelCreator {
        AssistantBarViewModel(tasks, readiness)
    }

    // No saved-state handle and no analytics client, and neither is an
    // oversight: this view model holds what a person types into a site's form,
    // so it is the one screen in this feature that must not be restorable and
    // must not be counted. See [TaskInputViewModel].
    @Provides
    @IntoMap
    @ViewModelKey(TaskInputViewModel::class)
    fun taskInput(
        forms: TaskInputRepository,
    ): ViewModelCreator = ViewModelCreator {
        TaskInputViewModel(forms)
    }

    @Provides
    @IntoMap
    @ViewModelKey(AskConversationViewModel::class)
    fun askConversation(
        tasks: TaskRepository,
    ): ViewModelCreator = ViewModelCreator {
        AskConversationViewModel(tasks)
    }

    @Provides
    @IntoMap
    @ViewModelKey(ReportAnswerViewModel::class)
    fun reportAnswer(
        core: CoreApiClient,
        browser: BrowserRepository,
    ): ViewModelCreator = ViewModelCreator {
        ReportAnswerViewModel(core, browser)
    }

    @Provides
    @IntoMap
    @ViewModelKey(TaskViewViewModel::class)
    fun taskView(
        tasks: TaskRepository,
        analytics: AnalyticsClient,
        readAloud: ReadAloud,
        browserRoleOffer: BrowserRoleOffer,
    ): ViewModelCreator = ViewModelCreator {
        TaskViewViewModel(tasks, analytics, readAloud, browserRoleOffer)
    }
}
