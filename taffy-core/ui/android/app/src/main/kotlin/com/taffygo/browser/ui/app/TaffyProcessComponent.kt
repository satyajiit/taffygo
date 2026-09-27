// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.Context
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.di.TaffyApplicationContext
import com.taffygo.browser.ui.core.common.di.TaffyProcessLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProcessScope
import com.taffygo.browser.ui.core.common.di.CommonBindings
import com.taffygo.browser.ui.core.common.di.CoroutineScopeProvider
import dagger.BindsInstance
import dagger.Component

/** Process graph created by native browser composition. */
@TaffyProcessScope
@Component(
    modules = [
        CommonBindings::class,
        CoroutineScopeProvider::class,
        TaskContinuationProcessBindings::class,
    ],
)
interface TaffyProcessComponent {
    fun lifetime(): TaffyProcessLifetime
    fun dispatchers(): AppDispatchers
    fun regularProfileBuilder(): TaffyProfileComponent.Builder
    fun privateProfileBuilder(): TaffyPrivateProfileComponent.Builder

    @Component.Factory
    interface Factory {
        fun create(
            @BindsInstance @TaffyApplicationContext applicationContext: Context,
        ): TaffyProcessComponent
    }
}
