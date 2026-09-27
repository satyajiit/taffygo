// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets.di

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.assets.RequiredPartsInstaller
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.assets.internal.CoreTaffyPartsRepository
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import dagger.Module
import dagger.Provides

/** Profile binding for immutable browser Core API delivery state. */
@Module
object TaffyPartsBindings {
    @Provides
    @TaffyProfileScope
    fun provideTaffyPartsRepository(
        core: CoreApiClient,
        lifetime: TaffyProfileLifetime,
    ): TaffyPartsRepository = CoreTaffyPartsRepository(core, lifetime)

    @Provides
    @TaffyProfileScope
    fun provideRequiredPartsInstaller(
        parts: TaffyPartsRepository,
        lifetime: TaffyProfileLifetime,
    ): RequiredPartsInstaller = RequiredPartsInstaller(parts, lifetime)
}
