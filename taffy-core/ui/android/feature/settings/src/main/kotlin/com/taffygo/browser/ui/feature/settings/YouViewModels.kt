// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.preferences.LocalProfileRepository
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by You. */
@Module
object YouViewModels {
    @Provides
    @TaffyWindowScope
    fun memoryRepository(
        core: CoreApiClient,
        lifetime: TaffyWindowLifetime,
    ): MemoryRepository = CoreMemoryRepository(core, lifetime.scope)

    @Provides
    @TaffyWindowScope
    fun savedSignInsRepository(
        core: CoreApiClient,
        lifetime: TaffyWindowLifetime,
    ): SavedSignInsRepository = CoreSavedSignInsRepository(core, lifetime.scope)

    @Provides
    @TaffyWindowScope
    fun savedDetailsRepository(
        core: CoreApiClient,
        lifetime: TaffyWindowLifetime,
    ): SavedDetailsRepository = CoreSavedDetailsRepository(core, lifetime.scope)

    @Provides
    @IntoMap
    @ViewModelKey(YouViewModel::class)
    fun you(
        analytics: AnalyticsClient,
        browser: BrowserRepository,
        profile: LocalProfileRepository,
        time: TimeOnSitesRepository,
        memory: MemoryRepository,
        signIns: SavedSignInsRepository,
        details: SavedDetailsRepository,
    ): ViewModelCreator = ViewModelCreator {
        YouViewModel(
            profile = profile,
            time = time,
            memory = memory,
            signIns = signIns,
            details = details,
            browser = browser,
            analytics = analytics,
        )
    }

    @Provides
    @IntoMap
    @ViewModelKey(TimeOnSitesViewModel::class)
    fun timeOnSites(
        analytics: AnalyticsClient,
        repository: TimeOnSitesRepository,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        TimeOnSitesViewModel(repository, analytics, savedState)
    }

    @Provides
    @TaffyWindowScope
    fun whatHappenedRepository(
        core: CoreApiClient,
        workspaces: WorkspaceRepository,
        browser: BrowserRepository,
        lifetime: TaffyWindowLifetime,
    ): WhatHappenedRepository = ProfileWhatHappenedRepository(
        core = core,
        workspaces = workspaces,
        browser = browser,
        scope = lifetime.scope,
    )

    @Provides
    @IntoMap
    @ViewModelKey(WhatHappenedViewModel::class)
    fun whatHappened(
        analytics: AnalyticsClient,
        repository: WhatHappenedRepository,
    ): ViewModelCreator = ViewModelCreator { _ ->
        WhatHappenedViewModel(repository, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(SavedSignInsViewModel::class)
    fun savedSignIns(
        analytics: AnalyticsClient,
        repository: SavedSignInsRepository,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        SavedSignInsViewModel(
            repository = repository,
            analytics = analytics,
            savedState = savedState,
        )
    }

    @Provides
    @IntoMap
    @ViewModelKey(SavedDetailsViewModel::class)
    fun savedDetails(
        analytics: AnalyticsClient,
        repository: SavedDetailsRepository,
    ): ViewModelCreator = ViewModelCreator {
        SavedDetailsViewModel(repository, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(MemoryViewModel::class)
    fun memory(
        analytics: AnalyticsClient,
        repository: MemoryRepository,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        MemoryViewModel(repository, analytics, savedState)
    }
}
