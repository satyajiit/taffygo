// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.di

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.Clock
import com.taffygo.browser.ui.core.common.CommonGraph
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.Logger
import com.taffygo.browser.ui.core.common.internal.LoggingCoroutineFailureSink
import dagger.Module
import dagger.Provides

/**
 * Regular Dagger bindings for process-owned portable utilities.
 */
@Module
object CommonBindings {

    @Provides
    @TaffyProcessScope
    fun provideAppDispatchers(): AppDispatchers = CommonGraph.dispatchers()

    @Provides
    @TaffyProcessScope
    fun provideClock(): Clock = CommonGraph.clock()

    @Provides
    @TaffyProcessScope
    fun provideLogger(): Logger = CommonGraph.logger()

    @Provides
    @TaffyProcessScope
    fun provideCoroutineFailureSink(logger: Logger): CoroutineFailureSink =
        LoggingCoroutineFailureSink(logger)
}
