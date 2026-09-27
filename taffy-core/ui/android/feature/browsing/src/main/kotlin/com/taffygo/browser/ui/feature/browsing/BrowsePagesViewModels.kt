// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.browser.AddressBarSuggestionSource
import com.taffygo.browser.ui.core.browser.BrowserMediator
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by History and Bookmarks. */
@Module
object BrowsePagesViewModels {
    /** One pre-indexed adapter shared by every address-bar view model in this window. */
    @Provides
    @TaffyWindowScope
    fun addressBarSuggestions(
        history: HistoryRepository,
        bookmarks: BookmarksRepository,
        browser: BrowserMediator,
        lifetime: TaffyWindowLifetime,
    ): AddressBarSuggestionSource = ProfileAddressBarSuggestionSource(
        tabs = browser.tabs,
        history = history.snapshot,
        bookmarks = bookmarks.snapshot,
        scope = lifetime.scope,
    )

    @Provides
    @IntoMap
    @ViewModelKey(HistoryViewModel::class)
    fun history(
        history: HistoryRepository,
        browser: BrowserRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        HistoryViewModel(history, browser, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(BookmarksViewModel::class)
    fun bookmarks(
        bookmarks: BookmarksRepository,
        browser: BrowserRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        BookmarksViewModel(bookmarks, browser, analytics, savedState)
    }
}
