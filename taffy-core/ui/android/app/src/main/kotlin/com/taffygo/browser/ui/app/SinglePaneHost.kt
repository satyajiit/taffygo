// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.feature.assistant.AssistantBar
import com.taffygo.browser.ui.feature.assistant.StartPageTaskPanel
import com.taffygo.browser.ui.feature.assistant.TaskWaitCard
import com.taffygo.browser.ui.feature.assistant.TaskViewScreen
import com.taffygo.browser.ui.feature.browsing.AddressBarScreen
import com.taffygo.browser.ui.feature.browsing.BookmarksScreen
import com.taffygo.browser.ui.feature.browsing.BrowserMainScreen
import com.taffygo.browser.ui.feature.browsing.ErrandPageScreen
import com.taffygo.browser.ui.feature.downloads.DownloadsScreen
import com.taffygo.browser.ui.feature.browsing.HistoryScreen
import com.taffygo.browser.ui.feature.browsing.NewTabScreen
import com.taffygo.browser.ui.feature.browsing.PageFailureNotice
import com.taffygo.browser.ui.feature.browsing.TabSwitcherScreen
import com.taffygo.browser.ui.feature.onboarding.AiSetupScreen
import com.taffygo.browser.ui.feature.onboarding.LanguageRegionScreen
import com.taffygo.browser.ui.feature.onboarding.MeetTaffyScreen
import com.taffygo.browser.ui.feature.onboarding.GetStartedScreen
import com.taffygo.browser.ui.feature.onboarding.WelcomeScreen
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
import com.taffygo.browser.ui.feature.settings.MemoryScreen
import com.taffygo.browser.ui.feature.settings.NotificationsScreen
import com.taffygo.browser.ui.feature.settings.PersonalityScreen
import com.taffygo.browser.ui.feature.settings.PersonalityTuningScreen
import com.taffygo.browser.ui.feature.settings.PrivacyScreen
import com.taffygo.browser.ui.feature.settings.SavedDetailsScreen
import com.taffygo.browser.ui.feature.settings.SavedSignInsScreen
import com.taffygo.browser.ui.feature.settings.SettingsHomeScreen
import com.taffygo.browser.ui.feature.settings.SiteSettingsScreen
import com.taffygo.browser.ui.feature.settings.SkillDetailScreen
import com.taffygo.browser.ui.feature.settings.SkillsListScreen
import com.taffygo.browser.ui.feature.settings.TaffySettingsScreen
import com.taffygo.browser.ui.feature.settings.TimeOnSitesScreen
import com.taffygo.browser.ui.feature.settings.WhatHappenedScreen
import com.taffygo.browser.ui.feature.settings.YouScreen
import com.taffygo.browser.ui.feature.workspaces.ExportSheetScreen
import com.taffygo.browser.ui.feature.workspaces.FactCorrectionScreen
import com.taffygo.browser.ui.feature.workspaces.KeepThisScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryCollectionScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryExportSheetScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryHomeScreen
import com.taffygo.browser.ui.feature.workspaces.LibraryItemScreen
import com.taffygo.browser.ui.feature.workspaces.SourceViewerScreen
import com.taffygo.browser.ui.feature.workspaces.WorkspaceDetailScreen
import com.taffygo.browser.ui.feature.workspaces.WorkspaceListScreen

/**
 * One destination, one screen. Used on a phone, and for destinations that
 * have no list-detail pair.
 *
 * [overlay] is the entry standing over the screen, if the stack has one (see
 * [com.taffygo.browser.ui.core.ui.ScreenPresentation]). The browsing surface
 * takes it as a slot, so that screen composes it where its own back handler
 * and its own overlays already are; every other screen has it drawn over the
 * top here.
 */
@Composable
fun SinglePaneHost(
    destination: TaffyDestination,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    overlay: TaffyDestination? = null,
) {
    Box {
        when (destination) {
            TaffyDestination.OnboardingWelcome -> WelcomeScreen(navigator, modifier)
            TaffyDestination.MeetTaffy -> MeetTaffyScreen(navigator, modifier)
            TaffyDestination.LanguageRegion -> LanguageRegionScreen(navigator, modifier)
            TaffyDestination.GetStarted -> GetStartedScreen(navigator, modifier)
            TaffyDestination.AiSetup -> AiSetupScreen(navigator, modifier)
            TaffyDestination.BrowserProfiles -> BrowserProfilesScreen(navigator, modifier)
            TaffyDestination.Backup -> BackupScreen(navigator, modifier)
            // Three slots, filled the same way and for the same reason: all three
            // surfaces belong to the assistant feature, and browsing does not
            // depend on it. The shell is where they meet.
            TaffyDestination.BrowserMain -> BrowserMainScreen(
                navigator = navigator,
                assistantBar = { AssistantBar(navigator = navigator) },
                taskWait = {
                    TaskWaitCard(navigator = navigator, destination = TaffyDestination.BrowserMain)
                },
                askOverlay = {
                    if (overlay != null) OverlayHost(overlay = overlay, navigator = navigator)
                },
                taskPanel = { started, tryAgain, leave ->
                    StartPageTaskPanel(
                        navigator = navigator,
                        taskId = started.id,
                        goal = started.goal,
                        onTryAgain = tryAgain,
                        onLeave = leave,
                    )
                },
                modifier = modifier,
            )
            // The start page's row is Tabs and the gear only; the Assistant pill
            // lives on the browsing surface's page row. What it does have is
            // the panel under its box, for a task started there.
            TaffyDestination.NewTab -> NewTabScreen(
                navigator = navigator,
                modifier = modifier,
                taskPanel = { started, tryAgain, leave ->
                    StartPageTaskPanel(
                        navigator = navigator,
                        taskId = started.id,
                        goal = started.goal,
                        onTryAgain = tryAgain,
                        onLeave = leave,
                    )
                },
            )
            TaffyDestination.AddressBar -> AddressBarScreen(navigator, modifier)
            TaffyDestination.TabSwitcher -> TabSwitcherScreen(navigator, modifier)
            // Full width, always. An errand is one page with one way back; a
            // detail pane beside a list would be a second thing to look at on a
            // screen whose whole point is that there is only one.
            is TaffyDestination.ErrandPage ->
                ErrandPageScreen(destination, navigator, modifier)
            is TaffyDestination.PageError -> PageFailureNotice(
                failure = destination.failure,
                onReload = { navigator.goBack() },
                modifier = modifier,
            )
            TaffyDestination.Downloads -> DownloadsScreen(navigator, modifier)
            TaffyDestination.History -> HistoryScreen(navigator, modifier)
            TaffyDestination.Bookmarks -> BookmarksScreen(navigator, modifier)
            // An ask never owns the frame — `presentation()` lifts it off as
            // the overlay — so the bar this arm draws is the collapsed one.
            is TaffyDestination.AssistantBar -> AssistantBar(navigator, modifier)
            TaffyDestination.TaskView -> TaskViewScreen(navigator, modifier)
            TaffyDestination.WorkspaceList -> WorkspaceListScreen(navigator, modifier)
            is TaffyDestination.WorkspaceDetail ->
                WorkspaceDetailScreen(destination, navigator, modifier)
            is TaffyDestination.SourceViewer ->
                SourceViewerScreen(destination, navigator, modifier)
            is TaffyDestination.FactCorrection ->
                FactCorrectionScreen(destination, navigator, modifier)
            is TaffyDestination.ExportSheet ->
                ExportSheetScreen(destination, navigator, modifier)
            TaffyDestination.LibraryHome -> LibraryHomeScreen(navigator, modifier)
            is TaffyDestination.LibraryCollection ->
                LibraryCollectionScreen(destination, navigator, modifier)
            is TaffyDestination.LibraryExport ->
                LibraryExportSheetScreen(destination, navigator, modifier)
            is TaffyDestination.LibraryItem ->
                LibraryItemScreen(destination, navigator, modifier)
            TaffyDestination.KeepThis -> KeepThisScreen(navigator, modifier)
            TaffyDestination.SettingsHome -> SettingsHomeScreen(navigator, modifier)
            TaffyDestination.AiAndProviders -> ProviderHubScreen(navigator, modifier)
            is TaffyDestination.ProviderConfig ->
                ProviderConfigScreen(destination, navigator, modifier)
            is TaffyDestination.ProviderSignIn ->
                ProviderSignInScreen(destination, navigator, modifier)
            is TaffyDestination.ModelSelection ->
                ModelSelectionScreen(destination, navigator, modifier)
            is TaffyDestination.CustomEndpointSetup ->
                CustomEndpointSetupScreen(destination, navigator, modifier)
            TaffyDestination.ConnectedProviders -> ConnectedProvidersScreen(navigator, modifier)
            TaffyDestination.AdAndTrackerBlocking -> FilteringSettingsScreen(navigator, modifier)
            TaffyDestination.Notifications -> NotificationsScreen(navigator, modifier)
            TaffyDestination.Appearance -> AppearanceScreen(navigator, modifier)
            TaffyDestination.General -> GeneralScreen(navigator, modifier)
            TaffyDestination.Privacy -> PrivacyScreen(navigator, modifier)
            TaffyDestination.TaffySettings -> TaffySettingsScreen(navigator, modifier)
            TaffyDestination.About -> AboutScreen(navigator, modifier)
            TaffyDestination.HelpAndFeedback -> HelpScreen(navigator, modifier)
            TaffyDestination.SiteSettings -> SiteSettingsScreen(navigator, modifier)
            TaffyDestination.ClearBrowsingData -> ClearBrowsingDataScreen(navigator, modifier)
            TaffyDestination.You -> YouScreen(navigator, modifier)
            TaffyDestination.TimeOnSites -> TimeOnSitesScreen(navigator, modifier)
            TaffyDestination.WhatHappened -> WhatHappenedScreen(navigator, modifier)
            TaffyDestination.SavedSignIns -> SavedSignInsScreen(navigator, modifier)
            TaffyDestination.SavedDetails -> SavedDetailsScreen(navigator, modifier)
            TaffyDestination.Memory -> MemoryScreen(navigator, modifier)
            TaffyDestination.SkillsList -> SkillsListScreen(navigator, modifier)
            is TaffyDestination.SkillDetail -> SkillDetailScreen(destination, navigator, modifier)
            TaffyDestination.Personality -> PersonalityScreen(navigator, modifier)
            TaffyDestination.PersonalityTuning -> PersonalityTuningScreen(navigator, modifier)
        }
        if (overlay != null && destination !is TaffyDestination.BrowserMain) {
            OverlayHost(overlay = overlay, navigator = navigator)
        }
    }
}
