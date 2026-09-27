// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyDestinationGroups
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPaneSplit
import com.taffygo.browser.ui.core.ui.TaffyTwoPane
import com.taffygo.browser.ui.feature.providers.ConnectedProvidersScreen
import com.taffygo.browser.ui.feature.providers.CustomEndpointSetupScreen
import com.taffygo.browser.ui.feature.providers.ModelSelectionScreen
import com.taffygo.browser.ui.feature.providers.ProviderConfigScreen
import com.taffygo.browser.ui.feature.providers.ProviderHubScreen
import com.taffygo.browser.ui.feature.providers.ProviderSignInScreen
import com.taffygo.browser.ui.feature.settings.AboutScreen
import com.taffygo.browser.ui.feature.settings.AppearanceScreen
import com.taffygo.browser.ui.feature.settings.BrowserProfilesScreen
import com.taffygo.browser.ui.feature.settings.ClearBrowsingDataScreen
import com.taffygo.browser.ui.feature.settings.FilteringSettingsScreen
import com.taffygo.browser.ui.feature.settings.GeneralScreen
import com.taffygo.browser.ui.feature.settings.HelpScreen
import com.taffygo.browser.ui.feature.settings.NotificationsScreen
import com.taffygo.browser.ui.feature.settings.PersonalityScreen
import com.taffygo.browser.ui.feature.settings.PersonalityTuningScreen
import com.taffygo.browser.ui.feature.settings.PrivacyScreen
import com.taffygo.browser.ui.feature.settings.SettingsHomeScreen
import com.taffygo.browser.ui.feature.settings.SettingsPanePlaceholder
import com.taffygo.browser.ui.feature.settings.SettingsSection
import com.taffygo.browser.ui.feature.settings.SiteSettingsScreen
import com.taffygo.browser.ui.feature.settings.SkillDetailScreen
import com.taffygo.browser.ui.feature.settings.SkillsListScreen
import com.taffygo.browser.ui.feature.settings.TaffySettingsScreen

/**
 * Settings as a list-detail pair: the sections stay visible while one of
 * them is open. Compact widths never reach this; the host draws a single
 * screen instead. Both widths keep the labeled list — never a 2-up grid.
 */
@Composable
fun SettingsTwoPane(
    destination: TaffyDestination,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    TaffyTwoPane(
        primary = {
            SettingsHomeScreen(
                navigator = navigator,
                selected = SettingsSection.of(destination),
                onBack = { navigator.popWhile(TaffyDestinationGroups::isSettings) },
            )
        },
        secondary = {
            when (destination) {
                TaffyDestination.SettingsHome -> SettingsPanePlaceholder()
                TaffyDestination.AiAndProviders ->
                    ProviderHubScreen(navigator = navigator, showUp = false)
                is TaffyDestination.ProviderConfig ->
                    ProviderConfigScreen(destination, navigator, showUp = false)
                is TaffyDestination.ProviderSignIn ->
                    ProviderSignInScreen(destination, navigator, showUp = false)
                is TaffyDestination.ModelSelection ->
                    ModelSelectionScreen(destination, navigator, showUp = false)
                is TaffyDestination.CustomEndpointSetup ->
                    CustomEndpointSetupScreen(destination, navigator, showUp = false)
                TaffyDestination.ConnectedProviders ->
                    ConnectedProvidersScreen(navigator = navigator, showUp = false)
                TaffyDestination.AdAndTrackerBlocking ->
                    FilteringSettingsScreen(navigator = navigator, showUp = false)
                TaffyDestination.Notifications ->
                    NotificationsScreen(navigator = navigator, showUp = false)
                TaffyDestination.Appearance ->
                    AppearanceScreen(navigator = navigator, showUp = false)
                TaffyDestination.General ->
                    GeneralScreen(navigator = navigator, showUp = false)
                TaffyDestination.BrowserProfiles ->
                    BrowserProfilesScreen(navigator = navigator, showUp = false)
                TaffyDestination.Backup ->
                    BackupScreen(navigator = navigator, showUp = false)
                TaffyDestination.Privacy ->
                    PrivacyScreen(navigator = navigator, showUp = false)
                TaffyDestination.TaffySettings ->
                    TaffySettingsScreen(navigator = navigator, showUp = false)
                TaffyDestination.About ->
                    AboutScreen(navigator = navigator, showUp = false)
                TaffyDestination.HelpAndFeedback ->
                    HelpScreen(navigator = navigator, showUp = false)
                TaffyDestination.SiteSettings ->
                    SiteSettingsScreen(navigator = navigator, showUp = false)
                TaffyDestination.ClearBrowsingData ->
                    ClearBrowsingDataScreen(navigator = navigator, showUp = false)
                TaffyDestination.SkillsList ->
                    SkillsListScreen(navigator = navigator, showUp = false)
                is TaffyDestination.SkillDetail ->
                    SkillDetailScreen(destination, navigator, modifier = Modifier, showUp = false)
                TaffyDestination.Personality ->
                    PersonalityScreen(navigator = navigator, showUp = false)
                TaffyDestination.PersonalityTuning ->
                    PersonalityTuningScreen(navigator = navigator, showUp = false)
                else -> SettingsPanePlaceholder()
            }
        },
        split = TaffyPaneSplit.LIST_DETAIL,
        modifier = modifier,
    )
}
