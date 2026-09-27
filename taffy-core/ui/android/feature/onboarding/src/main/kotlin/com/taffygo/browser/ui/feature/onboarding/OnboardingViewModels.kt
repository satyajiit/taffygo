// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.preferences.LocalProfileRepository
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by onboarding. */
@Module
object OnboardingViewModels {
    @Provides
    @IntoMap
    @ViewModelKey(AiSetupViewModel::class)
    fun aiSetup(
        preferences: UserPreferencesRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        AiSetupViewModel(preferences, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(LanguageRegionViewModel::class)
    fun languageRegion(
        preferences: UserPreferencesRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        LanguageRegionViewModel(preferences, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(MeetTaffyViewModel::class)
    fun meetTaffy(analytics: AnalyticsClient): ViewModelCreator =
        ViewModelCreator { MeetTaffyViewModel(analytics) }

    @Provides
    @IntoMap
    @ViewModelKey(GetStartedViewModel::class)
    fun getStarted(
        profile: LocalProfileRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        GetStartedViewModel(profile, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(WelcomeViewModel::class)
    fun welcome(
        preferences: UserPreferencesRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { WelcomeViewModel(preferences, analytics) }
}
