// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.Context
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.Clock
import com.taffygo.browser.ui.core.common.di.TaffyApplicationContext
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.credentials.ProviderKeyProber
import com.taffygo.browser.ui.core.preferences.LocalProfileRepository
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import com.taffygo.browser.ui.core.task.taffyReadinessRepository
import dagger.Module
import dagger.Provides
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn

/** Regular-Dagger bindings for one Chromium profile's real Android ports. */
@Module
object ProfilePlatformBindings {
    // The device's own country is the first-run region, and it is read here
    // rather than inside the repository so that class stays a projection over
    // the preference store with no Android API behind it. See `DeviceRegion.kt`
    // for why none of the four signals it reads needs a permission.
    @Provides
    @TaffyProfileScope
    fun providePreferences(
        store: ProfilePreferenceStore,
        @TaffyApplicationContext context: Context,
    ): UserPreferencesRepository =
        ProfileUserPreferencesRepository(store, detectedRegionCode(context))

    /*
     * Profile-scoped, and that scope is load-bearing. The repository reads
     * the store and runs the one-shot `banner_id` migration in its
     * constructor, so a second instance in the same process would find the
     * legacy key already consumed. One per profile means one migration.
     */
    @Provides
    @TaffyProfileScope
    fun provideLocalProfile(
        @TaffyApplicationContext context: Context,
    ): LocalProfileRepository =
        ProfileLocalProfileRepository(PrefsLocalProfileStore(context))

    // Profile-scoped where the browser repository is window-scoped: the
    // counts are the person's, so two windows feed one store and the store
    // survives both. The window-scoped recorder is started in
    // `BrowserBindings.provideBrowserRepository`.
    @Provides
    @TaffyProfileScope
    fun provideFrequentSites(
        store: ProfilePreferenceStore,
        dispatchers: AppDispatchers,
        clock: Clock,
    ): FrequentSitesRepository = ProfileFrequentSitesRepository(store, dispatchers, clock)

    // Both ports, because storing a provider key and telling the core about it
    // are one act: see ProfileProviderCredentialsRepository for what each half
    // is worth without the other. One coordinator per profile, because its
    // per-provider critical sections are the serialization decision 0078
    // decides; a second instance would be a second writer.
    @Provides
    @TaffyProfileScope
    fun provideProviderCredentialCoordinator(
        secureMaterial: AndroidProfileSecureMaterialStore,
        coreApi: CoreApiClient,
        revocation: ProviderRevocationPort,
    ): ProviderCredentialCoordinator = ProviderCredentialCoordinator(
        secureMaterial,
        ProfileProviderCredentialsRepository(secureMaterial, coreApi),
        coreApi,
        revocation,
    )

    @Provides
    @TaffyProfileScope
    fun provideProviderCredentials(
        coordinator: ProviderCredentialCoordinator,
    ): ProviderCredentialsRepository = coordinator

    // The same instance behind a second, narrower port: the probe rides the
    // coordinator's vault and Core API seams, and a second implementation
    // would be a second party minting transients for keys.
    @Provides
    @TaffyProfileScope
    fun provideProviderKeyProber(
        coordinator: ProviderCredentialCoordinator,
    ): ProviderKeyProber = coordinator

    // Whether Taffy can reach a provider is a fact — a usable key, an own
    // address or a plan — read from the core's roster and the vault's held
    // handles, never from the saved route alone. Joined here rather than in
    // `:core:task` because two of its inputs are ports of modules that one
    // does not import; it takes them as flows.
    @Provides
    @TaffyProfileScope
    fun provideTaffyReadiness(
        core: CoreApiClient,
        credentials: ProviderCredentialsRepository,
        preferences: UserPreferencesRepository,
        lifetime: TaffyProfileLifetime,
    ): TaffyReadinessRepository = taffyReadinessRepository(
        core = core,
        heldCredentialProviderIds = credentials.configuredProviderIds,
        chosenRoute = preferences.preferences
            .map { it.providerRoute }
            .stateIn(
                lifetime.scope,
                SharingStarted.Eagerly,
                preferences.preferences.value.providerRoute,
            ),
        lifetime = lifetime,
    )

    @Provides
    fun provideCredentialHandleBroker(
        secureMaterial: AndroidProfileSecureMaterialStore,
    ): CredentialHandleBroker = secureMaterial
}
