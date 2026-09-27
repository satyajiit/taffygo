// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providers.di

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.Clock
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.providers.ProviderModelPreferences
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import com.taffygo.browser.ui.core.providers.internal.CoreProviderModelPreferences
import com.taffygo.browser.ui.core.providers.internal.CoreProviderRosterRepository
import dagger.Module
import dagger.Provides

/** Profile binding for the immutable browser Core API provider roster. */
@Module
object ProviderRosterBindings {
    // The clock stamps when this profile first saw a refusal; the core's own
    // reading is monotonic and cannot be shown as a time.
    @Provides
    @TaffyProfileScope
    fun provideProviderRosterRepository(
        core: CoreApiClient,
        lifetime: TaffyProfileLifetime,
        clock: Clock,
    ): ProviderRosterRepository = CoreProviderRosterRepository(core, lifetime, clock)

    /**
     * The write half, bound beside the read half rather than folded into it:
     * the roster answers what the core published and this states what a person
     * asked for, and keeping them apart is what stops a surface reporting the
     * second as though it were the first.
     */
    @Provides
    @TaffyProfileScope
    fun provideProviderModelPreferences(
        core: CoreApiClient,
    ): ProviderModelPreferences = CoreProviderModelPreferences(core)
}
