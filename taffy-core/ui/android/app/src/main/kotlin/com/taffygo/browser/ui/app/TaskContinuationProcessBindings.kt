// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.Context
import com.taffygo.browser.ui.core.common.di.TaffyApplicationContext
import com.taffygo.browser.ui.core.common.di.TaffyProcessScope
import dagger.Module
import dagger.Provides

/** One bounded notification ingress and Android poster for the browser process. */
@Module
internal object TaskContinuationProcessBindings {
    @Provides
    @TaffyProcessScope
    fun provideRegistry(): TaskContinuationRegistry = TaskContinuationRegistry.process

    @Provides
    @TaffyProcessScope
    fun providePlatform(
        @TaffyApplicationContext context: Context,
    ): TaskContinuationPlatform = AndroidTaskContinuationPlatform(context)
}
