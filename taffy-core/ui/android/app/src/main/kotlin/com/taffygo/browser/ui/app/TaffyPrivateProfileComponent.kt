// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.common.di.TaffyProfileIdentity
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import dagger.BindsInstance
import dagger.Subcomponent

/**
 * Ephemeral private-profile graph.
 *
 * It deliberately installs no account, preference, credential, secure-store, analytics,
 * workspace, task, or Window modules. A private tab is projected through the regular browser
 * window that currently owns the selector, while this graph owns only the private profile
 * lifetime and its movable Tab components.
 */
@TaffyProfileScope
@Subcomponent
interface TaffyPrivateProfileComponent : TaffyTabComponentParent {
    fun lifetime(): TaffyProfileLifetime

    override fun tabBuilder(): TaffyTabComponent.Builder

    @Subcomponent.Builder
    interface Builder {
        @BindsInstance
        fun identity(identity: TaffyProfileIdentity): Builder

        fun build(): TaffyPrivateProfileComponent
    }
}
