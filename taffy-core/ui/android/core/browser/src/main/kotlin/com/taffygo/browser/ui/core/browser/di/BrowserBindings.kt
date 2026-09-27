// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.di

import android.content.Context
import com.taffygo.browser.ui.core.browser.AddressBarSuggestionSource
import com.taffygo.browser.ui.core.browser.BrowserMediator
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.browser.SearchEngineRepository
import com.taffygo.browser.ui.core.browser.internal.DefaultBrowserRepository
import com.taffygo.browser.ui.core.browser.internal.FrequentSitesTracker
import com.taffygo.browser.ui.core.browser.internal.SharedPreferencesSearchEngineRepository
import com.taffygo.browser.ui.core.common.di.TaffyApplicationContext
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import dagger.Module
import dagger.Provides

/** Window binding for the browser-owned tab and navigation mediator. */
@Module
object BrowserBindings {
    /**
     * The repository, with the visit recorder started beside it.
     *
     * The tracker starts here rather than from the Chromium host because this
     * is the moment a window has a tab list to watch: the repository exists
     * exactly when something in the window is looking at the browser, and the
     * window lifetime's scope cancels the watch when the window closes. The
     * profile-scoped [FrequentSitesRepository] outlives it, which is the
     * point — the counts belong to the person, not to one window.
     */
    /** The address-bar engine, default Google, surviving process death on this device. */
    @Provides
    @TaffyWindowScope
    fun provideSearchEngineRepository(
        @TaffyApplicationContext context: Context,
        lifetime: TaffyWindowLifetime,
    ): SearchEngineRepository = SharedPreferencesSearchEngineRepository(context).also(lifetime::own)

    @Provides
    @TaffyWindowScope
    fun provideBrowserRepository(
        mediator: BrowserMediator,
        searchEngines: SearchEngineRepository,
        addressBarSuggestions: AddressBarSuggestionSource,
        frequentSites: FrequentSitesRepository,
        lifetime: TaffyWindowLifetime,
    ): BrowserRepository {
        val repository = DefaultBrowserRepository(mediator, searchEngines, addressBarSuggestions)
        FrequentSitesTracker(repository.tabs, frequentSites).start(lifetime.scope)
        return repository
    }
}
