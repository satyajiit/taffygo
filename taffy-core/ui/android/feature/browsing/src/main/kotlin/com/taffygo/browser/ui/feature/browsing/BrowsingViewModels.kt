// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.page.PageIntelligenceRepository
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.ErrandPagePort
import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import com.taffygo.browser.ui.core.task.TaskInputRepository
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import com.taffygo.browser.ui.core.ui.VoiceInput
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by browsing. */
@Module
object BrowsingViewModels {
    @Provides
    @TaffyWindowScope
    fun savedFlowReviews(core: CoreApiClient, lifetime: TaffyWindowLifetime): SavedFlowReviewRepository =
        SavedFlowReviewRepository(core, lifetime.scope)

    @Provides
    @TaffyWindowScope
    fun savedFlowPages(): SavedFlowPageRequests = SavedFlowPageRequests()

    @Provides
    @IntoMap
    @ViewModelKey(BrowserMainViewModel::class)
    fun browserMain(
        browser: BrowserRepository,
        analytics: AnalyticsClient,
        parts: TaffyPartsRepository,
        frequentSites: FrequentSitesRepository,
        findInPage: FindInPagePort,
        bookmarks: BookmarksWriter,
        siteInfo: SiteInfoRepository,
        pageZoom: PageZoomRepository,
    ): ViewModelCreator = ViewModelCreator {
        BrowserMainViewModel(
            browser,
            analytics,
            parts,
            frequentSites,
            findInPage,
            bookmarks,
            siteInfo,
            pageZoom,
        )
    }

    @Provides
    @IntoMap
    @ViewModelKey(BrowserTakeoverViewModel::class)
    fun browserTakeover(
        tasks: TaskRepository,
        forms: TaskInputRepository,
        browser: BrowserRepository,
    ): ViewModelCreator = ViewModelCreator {
        BrowserTakeoverViewModel(tasks, forms, browser)
    }

    @Provides
    @IntoMap
    @ViewModelKey(PageSkillOffersViewModel::class)
    fun pageSkillOffers(
        browser: BrowserRepository,
        pages: PageIntelligenceRepository,
        core: CoreApiClient,
        reviews: SavedFlowReviewRepository,
        flowPages: SavedFlowPageRequests,
        tasks: TaskRepository,
        readiness: TaffyReadinessRepository,
    ): ViewModelCreator = ViewModelCreator {
        PageSkillOffersViewModel(browser, pages, reviews.status, tasks, readiness, reviews, flowPages)
    }

    @Provides
    @IntoMap
    @ViewModelKey(NewTabViewModel::class)
    fun newTab(
        browser: BrowserRepository,
        analytics: AnalyticsClient,
        parts: TaffyPartsRepository,
        frequentSites: FrequentSitesRepository,
    ): ViewModelCreator = ViewModelCreator {
        NewTabViewModel(browser, analytics, parts, frequentSites)
    }

    @Provides
    @IntoMap
    @ViewModelKey(AddressBarViewModel::class)
    fun addressBar(
        browser: BrowserRepository,
        analytics: AnalyticsClient,
        tasks: TaskRepository,
        readiness: TaffyReadinessRepository,
        voiceInput: VoiceInput,
        reviews: SavedFlowReviewRepository,
        flowPages: SavedFlowPageRequests,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        AddressBarViewModel(browser, analytics, savedState, tasks, readiness, voiceInput, reviews, flowPages)
    }

    @Provides
    @IntoMap
    @ViewModelKey(AttachPagesViewModel::class)
    fun attachPages(
        browser: BrowserRepository,
    ): ViewModelCreator = ViewModelCreator {
        AttachPagesViewModel(askPagesRepository(browser))
    }

    @Provides
    @IntoMap
    @ViewModelKey(ErrandPageViewModel::class)
    fun errandPage(errand: ErrandPagePort): ViewModelCreator = ViewModelCreator { savedState ->
        ErrandPageViewModel(errand, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(TabSwitcherViewModel::class)
    fun tabSwitcher(
        browser: BrowserRepository,
        tasks: TaskRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        TabSwitcherViewModel(browser, tasks, analytics)
    }
}
