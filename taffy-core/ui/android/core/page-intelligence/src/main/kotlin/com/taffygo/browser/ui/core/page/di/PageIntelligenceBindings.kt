// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.page.di

import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.page.PageIntelligenceRepository
import com.taffygo.browser.ui.core.page.SelectedPageIntelligenceClient
import com.taffygo.browser.ui.core.page.internal.DefaultPageIntelligenceRepository
import dagger.Module
import dagger.Provides

/** Window binding over the selected tab's independently owned page client. */
@Module
object PageIntelligenceBindings {
    @Provides
    @TaffyWindowScope
    fun providePageIntelligenceRepository(
        client: SelectedPageIntelligenceClient,
    ): PageIntelligenceRepository = DefaultPageIntelligenceRepository(client)
}
