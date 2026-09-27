// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.app.Activity
import com.taffygo.browser.ui.core.browser.BrowserMediator
import com.taffygo.browser.ui.core.browser.ErrandPagePort
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.browser.di.BrowserBindings
import com.taffygo.browser.ui.core.common.di.TaffyWindowIdentity
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.page.di.PageIntelligenceBindings
import com.taffygo.browser.ui.core.ui.CountryFlagSource
import com.taffygo.browser.ui.core.ui.StartSceneSource
import com.taffygo.browser.ui.core.ui.DaggerViewModelFactory
import com.taffygo.browser.ui.feature.assistant.AssistantViewModels
import com.taffygo.browser.ui.feature.browsing.BrowsePagesViewModels
import com.taffygo.browser.ui.feature.browsing.BookmarksRepository
import com.taffygo.browser.ui.feature.browsing.BookmarksWriter
import com.taffygo.browser.ui.feature.browsing.BrowsingViewModels
import com.taffygo.browser.ui.feature.browsing.FindInPagePort
import com.taffygo.browser.ui.feature.browsing.HistoryRepository
import com.taffygo.browser.ui.feature.browsing.PageZoomRepository
import com.taffygo.browser.ui.feature.browsing.SiteInfoRepository
import com.taffygo.browser.ui.feature.downloads.DownloadViewModels
import com.taffygo.browser.ui.feature.onboarding.OnboardingViewModels
import com.taffygo.browser.ui.feature.providers.ProvidersViewModels
import com.taffygo.browser.ui.feature.settings.SettingsViewModels
import com.taffygo.browser.ui.feature.settings.AboutRepository
import com.taffygo.browser.ui.feature.settings.ClearDataRepository
import com.taffygo.browser.ui.feature.settings.GeneralSettingsRepository
import com.taffygo.browser.ui.feature.settings.ProfileDataControl
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import com.taffygo.browser.ui.feature.settings.TaffyHubViewModels
import com.taffygo.browser.ui.feature.settings.TimeOnSitesRepository
import com.taffygo.browser.ui.feature.settings.YouViewModels
import com.taffygo.browser.ui.feature.workspaces.LibraryViewModels
import com.taffygo.browser.ui.feature.workspaces.WorkspaceViewModels
import com.taffygo.browser.ui.core.page.SelectedPageIntelligenceClient
import dagger.BindsInstance
import dagger.Subcomponent

/** Window graph owned and closed by one browser activity. */
@TaffyWindowScope
@Subcomponent(
    modules = [
        ShellViewModels::class,
        BrowserBindings::class,
        PageIntelligenceBindings::class,
        AssistantViewModels::class,
        BrowsingViewModels::class,
        BrowsePagesViewModels::class,
        OnboardingViewModels::class,
        ProvidersViewModels::class,
        SettingsViewModels::class,
        YouViewModels::class,
        TaffyHubViewModels::class,
        WorkspaceViewModels::class,
        LibraryViewModels::class,
        VoicePlatformBindings::class,
        BrowserRolePlatformBindings::class,
        PermissionPlatformBindings::class,
        DownloadViewModels::class,
        DownloadPlatformBindings::class,
    ],
)
interface TaffyWindowComponent {
    fun identity(): TaffyWindowIdentity
    fun lifetime(): TaffyWindowLifetime
    fun viewModelFactory(): DaggerViewModelFactory
    fun countryFlags(): CountryFlagSource

    fun startScenes(): StartSceneSource
    fun preferences(): UserPreferencesRepository
    fun browserRepository(): BrowserRepository
    fun dispatchers(): AppDispatchers
    fun credentialAdapter(): AndroidCredentialAdapter
    fun authSurfaceAdapter(): AndroidAuthSurfaceAdapter
    fun permissionAdapter(): AndroidPermissionAdapter
    fun voiceInputAdapter(): AndroidVoiceInputAdapter
    fun readAloudAdapter(): AndroidReadAloudAdapter
    fun backupWindowHost(): BackupWindowHost
    fun backupDocumentPicker(): BackupDocumentPicker

    @Subcomponent.Builder
    interface Builder {
        @BindsInstance
        fun activity(activity: Activity): Builder

        @BindsInstance
        fun identity(identity: TaffyWindowIdentity): Builder

        @BindsInstance
        fun browserMediator(mediator: BrowserMediator): Builder

        @BindsInstance
        fun historyRepository(repository: HistoryRepository): Builder

        @BindsInstance
        fun bookmarksRepository(repository: BookmarksRepository): Builder

        @BindsInstance
        fun bookmarksWriter(writer: BookmarksWriter): Builder

        @BindsInstance
        fun findInPagePort(port: FindInPagePort): Builder

        @BindsInstance
        fun errandPagePort(port: ErrandPagePort): Builder

        @BindsInstance
        fun siteInfoRepository(repository: SiteInfoRepository): Builder

        @BindsInstance
        fun pageZoomRepository(repository: PageZoomRepository): Builder

        @BindsInstance
        fun generalSettingsRepository(repository: GeneralSettingsRepository): Builder

        @BindsInstance
        fun browserProfilesRepository(repository: BrowserProfilesRepository): Builder

        @BindsInstance
        fun siteSettingsRepository(repository: SiteSettingsRepository): Builder

        @BindsInstance
        fun profileDataControl(control: ProfileDataControl): Builder

        @BindsInstance
        fun clearDataRepository(repository: ClearDataRepository): Builder

        @BindsInstance
        fun timeOnSitesRepository(repository: TimeOnSitesRepository): Builder

        @BindsInstance
        fun aboutRepository(repository: AboutRepository): Builder

        @BindsInstance
        fun backupWindowHost(host: BackupWindowHost): Builder

        @BindsInstance
        fun backupDocumentPicker(picker: BackupDocumentPicker): Builder

        /** Browser-owned selected-tab router; each delegate remains tab-scoped. */
        @BindsInstance
        fun selectedPageIntelligence(client: SelectedPageIntelligenceClient): Builder

        fun build(): TaffyWindowComponent
    }
}
