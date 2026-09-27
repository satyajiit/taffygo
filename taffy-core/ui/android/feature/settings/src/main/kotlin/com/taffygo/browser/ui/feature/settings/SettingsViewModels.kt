// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.PlatformPermissionRequester
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.SearchEngineRepository
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.preferences.LocalProfileRepository
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by settings. */
@Module
object SettingsViewModels {
    @Provides
    @TaffyWindowScope
    fun privacyCenter(
        core: CoreApiClient,
        browser: BrowserRepository,
        sites: SiteSettingsRepository,
        dataControl: ProfileDataControl,
        lifetime: TaffyWindowLifetime,
    ): PrivacyCenterRepository = ProfilePrivacyCenterRepository(
        coreStatus = core.status,
        downloads = browser.downloads,
        siteSettings = sites.snapshot,
        dataControl = dataControl,
        scope = lifetime.scope,
    )

    @Provides
    @IntoMap
    @ViewModelKey(AppearanceViewModel::class)
    fun appearance(
        preferences: UserPreferencesRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { AppearanceViewModel(preferences, analytics) }

    @Provides
    @IntoMap
    @ViewModelKey(FilteringSettingsViewModel::class)
    fun filteringSettings(
        browser: BrowserRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        FilteringSettingsViewModel(
            browser,
            BrowserBlockingWeekRepository(browser.filtering),
            analytics,
        )
    }

    @Provides
    @IntoMap
    @ViewModelKey(NotificationsViewModel::class)
    fun notifications(
        preferences: UserPreferencesRepository,
        permissions: PlatformPermissionRequester,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        NotificationsViewModel(preferences, permissions, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(SettingsHomeViewModel::class)
    fun settingsHome(
        analytics: AnalyticsClient,
        browser: BrowserRepository,
        profile: LocalProfileRepository,
    ): ViewModelCreator =
        ViewModelCreator { savedState ->
            SettingsHomeViewModel(
                analytics,
                savedState,
                BrowserBlockingWeekRepository(browser.filtering),
                profile,
            )
        }

    @Provides
    @IntoMap
    @ViewModelKey(BrowserProfilesViewModel::class)
    fun browserProfiles(
        profiles: BrowserProfilesRepository,
        workspaces: WorkspaceRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        BrowserProfilesViewModel(profiles, workspaces, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(GeneralViewModel::class)
    fun general(
        general: GeneralSettingsRepository,
        searchEngines: SearchEngineRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        GeneralViewModel(general, searchEngines, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(SearchEngineViewModel::class)
    fun searchEngine(
        searchEngines: SearchEngineRepository,
        preferences: UserPreferencesRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        SearchEngineViewModel(searchEngines, preferences, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(PrivacyViewModel::class)
    fun privacy(
        privacy: PrivacyCenterRepository,
        preferences: UserPreferencesRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        PrivacyViewModel(privacy, preferences, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(SiteSettingsViewModel::class)
    fun siteSettings(
        sites: SiteSettingsRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        SiteSettingsViewModel(sites, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(ClearBrowsingDataViewModel::class)
    fun clearBrowsingData(
        clearData: ClearDataRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        ClearBrowsingDataViewModel(clearData, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(AboutViewModel::class)
    fun about(
        about: AboutRepository,
        browser: BrowserRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        AboutViewModel(about, browser, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(HelpViewModel::class)
    fun help(
        browser: BrowserRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { HelpViewModel(browser, analytics) }
}
