// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.di

import com.taffygo.browser.ui.core.common.ApplicationScope
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureScope
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.internal.CoroutineFailureHandlers
import dagger.Module
import dagger.Provides
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob

/**
 * The application scope, built from the injected dispatchers so a test replaces
 * both at once. A supervisor job means one failed child does not take the rest
 * of the application's long-lived work down with it.
 */
@Module
object CoroutineScopeProvider {

    @Provides
    @TaffyProcessScope
    @ApplicationScope
    fun provideApplicationScope(
        dispatchers: AppDispatchers,
        failureSink: CoroutineFailureSink,
    ): CoroutineScope = CoroutineScope(
        SupervisorJob() +
            dispatchers.default +
            CoroutineFailureHandlers.create(CoroutineFailureScope.PROCESS, failureSink),
    )
}
