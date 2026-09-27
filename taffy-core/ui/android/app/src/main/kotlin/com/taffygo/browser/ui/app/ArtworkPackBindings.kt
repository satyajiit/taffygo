// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.ui.CountryFlagSource
import com.taffygo.browser.ui.core.ui.StartSceneSource
import dagger.Module
import dagger.Provides

/**
 * Profile bindings for the published artwork packs.
 *
 * Both are the same shape over the same delivery plane — a pack of pictures the
 * browser process fetched and verified, read a member at a time — so they are
 * bound together rather than one module each.
 */
@Module
object ArtworkPackBindings {
    @Provides
    @TaffyProfileScope
    fun provideCountryFlagSource(
        parts: TaffyPartsRepository,
        lifetime: TaffyProfileLifetime,
        dispatchers: AppDispatchers,
    ): CountryFlagSource = DeliveryCountryFlagSource(parts, lifetime, dispatchers)

    @Provides
    @TaffyProfileScope
    fun provideStartSceneSource(
        parts: TaffyPartsRepository,
        lifetime: TaffyProfileLifetime,
        dispatchers: AppDispatchers,
    ): StartSceneSource = DeliveryStartSceneSource(parts, lifetime, dispatchers)
}
