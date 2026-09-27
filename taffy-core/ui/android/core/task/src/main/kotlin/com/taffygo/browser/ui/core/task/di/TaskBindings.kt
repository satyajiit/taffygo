// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.di

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.internal.CoreTaskRepository
import dagger.Module
import dagger.Provides

/** Profile binding for the canonical browser Core API task projection. */
@Module
object TaskBindings {
    @Provides
    @TaffyProfileScope
    fun provideTaskRepository(
        core: CoreApiClient,
        analytics: AnalyticsClient,
        lifetime: TaffyProfileLifetime,
    ): TaskRepository = CoreTaskRepository(core, analytics, lifetime)
}
