// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.workspace.di

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import com.taffygo.browser.ui.core.workspace.internal.CoreWorkspaceRepository
import dagger.Module
import dagger.Provides

/** Profile binding for immutable browser Core API workspace state. */
@Module
object WorkspaceBindings {
    @Provides
    @TaffyProfileScope
    fun provideWorkspaceRepository(
        core: CoreApiClient,
        lifetime: TaffyProfileLifetime,
    ): WorkspaceRepository = CoreWorkspaceRepository(core, lifetime)
}
