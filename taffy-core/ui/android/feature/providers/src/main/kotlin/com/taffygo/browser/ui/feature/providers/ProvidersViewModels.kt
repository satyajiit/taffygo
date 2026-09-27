// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.browser.ErrandPagePort
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.credentials.ProviderKeyProber
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.providerauth.ProviderSignInEngine
import com.taffygo.browser.ui.core.providers.ProviderModelPreferences
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Window-scoped assisted view-model creators owned by the provider surfaces. */
@Module
object ProvidersViewModels {

    @Provides
    @IntoMap
    @ViewModelKey(ProviderHubViewModel::class)
    fun providerHub(
        roster: ProviderRosterRepository,
        credentials: ProviderCredentialsRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        ProviderHubViewModel(roster, credentials, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(ProviderConfigViewModel::class)
    fun providerConfig(
        roster: ProviderRosterRepository,
        credentials: ProviderCredentialsRepository,
        prober: ProviderKeyProber,
        preferences: UserPreferencesRepository,
        errand: ErrandPagePort,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        ProviderConfigViewModel(
            roster,
            credentials,
            prober,
            preferences,
            errand,
            analytics,
            savedState,
        )
    }

    @Provides
    @IntoMap
    @ViewModelKey(ModelSelectionViewModel::class)
    fun modelSelection(
        roster: ProviderRosterRepository,
        credentials: ProviderCredentialsRepository,
        modelPreferences: ProviderModelPreferences,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        ModelSelectionViewModel(roster, credentials, modelPreferences, analytics, savedState)
    }

    /**
     * Screen SCR-418 talks to the browser Core API directly, through the seam
     * this feature owns rather than a repository in a core module. The
     * composer's suggestion lifecycle is bound the same way and for the same
     * reason: what crosses it is one screen's vocabulary — an address, a name,
     * a list of model identities — and nothing else in the product asks for
     * it.
     *
     * The credential repository is the same one screens SCR-415, SCR-417 and
     * SCR-419 take, and it is not bound to catalog providers by anything: a key
     * for a server somebody runs themselves is sealed by the same store, under
     * the same kind of handle. What this screen uses is the half that seals
     * without announcing, because its own save carries the announcement.
     */
    @Provides
    @IntoMap
    @ViewModelKey(CustomEndpointViewModel::class)
    fun customEndpoint(
        roster: ProviderRosterRepository,
        core: CoreApiClient,
        credentials: ProviderCredentialsRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        CustomEndpointViewModel(
            roster,
            customEndpoints(core),
            credentials,
            analytics,
            savedState,
        )
    }

    @Provides
    @IntoMap
    @ViewModelKey(ConnectedProvidersViewModel::class)
    fun connectedProviders(
        roster: ProviderRosterRepository,
        credentials: ProviderCredentialsRepository,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator {
        ConnectedProvidersViewModel(roster, credentials, analytics)
    }

    @Provides
    @IntoMap
    @ViewModelKey(ProviderSignInViewModel::class)
    fun providerSignIn(
        roster: ProviderRosterRepository,
        signIn: ProviderSignInEngine,
        errand: ErrandPagePort,
        analytics: AnalyticsClient,
    ): ViewModelCreator = ViewModelCreator { savedState ->
        ProviderSignInViewModel(roster, signIn, errand, analytics, savedState)
    }
}
