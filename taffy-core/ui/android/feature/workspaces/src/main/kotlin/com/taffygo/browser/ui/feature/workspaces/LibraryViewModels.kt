// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by Library. */
@Module
object LibraryViewModels {
    @Provides
    @TaffyWindowScope
    fun libraryRepository(
        core: CoreApiClient,
        workspaces: WorkspaceRepository,
        lifetime: TaffyWindowLifetime,
    ): LibraryRepository = WorkspaceLibraryRepository(core, workspaces, lifetime.scope)

    @Provides
    @IntoMap
    @ViewModelKey(LibraryHomeViewModel::class)
    fun libraryHome(
        library: LibraryRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        LibraryHomeViewModel(library, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(LibraryCollectionViewModel::class)
    fun libraryCollection(
        library: LibraryRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        LibraryCollectionViewModel(library, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(LibraryItemViewModel::class)
    fun libraryItem(
        library: LibraryRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        LibraryItemViewModel(library, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(KeepThisViewModel::class)
    fun keepThis(
        library: LibraryRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { _ ->
        KeepThisViewModel(library, analytics)
    }
}
