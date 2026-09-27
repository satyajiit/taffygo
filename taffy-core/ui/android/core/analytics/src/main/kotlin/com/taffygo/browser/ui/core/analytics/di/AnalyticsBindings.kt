// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.analytics.di

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsGraph
import com.taffygo.browser.ui.core.common.Logger
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import dagger.Module
import dagger.Provides

/** Profile-scoped Dagger binding for the content-free diagnostic sink. */
@Module
object AnalyticsBindings {

    @Provides
    @TaffyProfileScope
    fun provideAnalyticsClient(logger: Logger): AnalyticsClient =
        AnalyticsGraph.analyticsClient(logger)
}
