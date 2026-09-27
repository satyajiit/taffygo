// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.common.di.TaffyTabIdentity
import com.taffygo.browser.ui.core.common.di.TaffyTabLifetime
import com.taffygo.browser.ui.core.common.di.TaffyTabScope
import com.taffygo.browser.ui.core.page.PageIntelligenceClient
import dagger.BindsInstance
import dagger.Subcomponent

/** Tab graph owned by WebContents and independent of any window. */
@TaffyTabScope
@Subcomponent
interface TaffyTabComponent {
    fun identity(): TaffyTabIdentity
    fun lifetime(): TaffyTabLifetime
    fun pageIntelligence(): PageIntelligenceClient

    @Subcomponent.Builder
    interface Builder {
        @BindsInstance
        fun identity(identity: TaffyTabIdentity): Builder

        @BindsInstance
        fun pageIntelligence(client: PageIntelligenceClient): Builder

        fun build(): TaffyTabComponent
    }
}
