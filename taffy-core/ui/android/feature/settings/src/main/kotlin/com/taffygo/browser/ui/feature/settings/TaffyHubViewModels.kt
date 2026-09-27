// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by the Taffy hub. */
@Module
object TaffyHubViewModels {

    @Provides
    @TaffyWindowScope
    fun skills(
        core: CoreApiClient,
        lifetime: TaffyProfileLifetime,
        reviews: SavedFlowReviewRepository,
    ): SkillsRepository = CoreSkillsRepository(core, lifetime, reviews)

    @Provides
    @TaffyWindowScope
    fun personality(
        core: CoreApiClient,
        lifetime: TaffyProfileLifetime,
    ): PersonalityRepository = CorePersonalityRepository(core, lifetime)

    @Provides
    @IntoMap
    @ViewModelKey(TaffySettingsViewModel::class)
    fun taffySettings(
        preferences: UserPreferencesRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { TaffySettingsViewModel(preferences, analytics) }

    @Provides
    @IntoMap
    @ViewModelKey(SkillsListViewModel::class)
    fun skillsList(
        skills: SkillsRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        SkillsListViewModel(skills, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(SkillDetailViewModel::class)
    fun skillDetail(
        skills: SkillsRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        SkillDetailViewModel(skills, analytics, savedState)
    }

    @Provides
    @IntoMap
    @ViewModelKey(PersonalityViewModel::class)
    fun personalityHub(
        personality: PersonalityRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { PersonalityViewModel(personality, analytics) }

    @Provides
    @IntoMap
    @ViewModelKey(PersonalityTuningViewModel::class)
    fun personalityTuning(
        personality: PersonalityRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        PersonalityTuningViewModel(personality, analytics)
    }
}
