// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by workspaces. */
@Module
object WorkspaceViewModels {
    @Provides
    @IntoMap
    @ViewModelKey(ExportSheetViewModel::class)
    fun exportSheet(
        workspaces: WorkspaceRepository,
        library: LibraryRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        ExportSheetViewModel(workspaces, library, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(FactCorrectionViewModel::class)
    fun factCorrection(
        workspaces: WorkspaceRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        FactCorrectionViewModel(workspaces, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(SourceViewerViewModel::class)
    fun sourceViewer(
        workspaces: WorkspaceRepository,
        browser: BrowserRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        SourceViewerViewModel(workspaces, browser, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(WorkspaceDetailViewModel::class)
    fun workspaceDetail(
        workspaces: WorkspaceRepository,
        library: LibraryRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        WorkspaceDetailViewModel(workspaces, library, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(WorkspaceListViewModel::class)
    fun workspaceList(
        workspaces: WorkspaceRepository,
        profiles: BrowserProfilesRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        WorkspaceListViewModel(workspaces, profiles, analytics, savedState)
    }
}
