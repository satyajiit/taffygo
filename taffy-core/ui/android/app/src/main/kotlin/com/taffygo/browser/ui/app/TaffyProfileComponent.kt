// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.di.AnalyticsBindings
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.BrowserTaskInputEndpoint
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.Logger
import com.taffygo.browser.ui.core.common.di.TaffyProfileIdentity
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.providerauth.ProviderManualCodePort
import com.taffygo.browser.ui.core.providerauth.ProviderSignInEngine
import com.taffygo.browser.ui.core.providerauth.di.ProviderSignInBindings
import com.taffygo.browser.ui.core.providers.di.ProviderRosterBindings
import com.taffygo.browser.ui.core.task.di.TaskBindings
import com.taffygo.browser.ui.core.task.di.TaskInputBindings
import com.taffygo.browser.ui.core.assets.RequiredPartsInstaller
import com.taffygo.browser.ui.core.assets.di.TaffyPartsBindings
import com.taffygo.browser.ui.core.ui.CountryFlagSource
import com.taffygo.browser.ui.core.ui.StartSceneSource
import com.taffygo.browser.ui.core.workspace.di.WorkspaceBindings
import dagger.BindsInstance
import dagger.Subcomponent

/** Regular-profile graph. Private profiles use [TaffyPrivateProfileComponent]. */
@TaffyProfileScope
@Subcomponent(
    modules = [
        AnalyticsBindings::class,
        ProviderRosterBindings::class,
        ProviderSignInBindings::class,
        TaskBindings::class,
        TaskInputBindings::class,
        TaffyPartsBindings::class,
        ArtworkPackBindings::class,
        WorkspaceBindings::class,
        ProfilePlatformBindings::class,
    ],
)
interface TaffyProfileComponent : TaffyTabComponentParent {
    fun identity(): TaffyProfileIdentity
    fun lifetime(): TaffyProfileLifetime

    /**
     * The three dispatchers, for a Chromium owner that has to place work on
     * a particular thread rather than merely run it.
     *
     * [TaffyProfileLifetime]'s scope is the default dispatcher, which is the
     * right answer for the reducing and formatting most profile-scoped work
     * does. A Mojo implementation is the exception: it is bound on the
     * thread that created it and must answer on that thread, and the browser
     * objects it reads are held to the same rule.
     */
    fun dispatchers(): AppDispatchers
    fun logger(): Logger
    fun preferences(): UserPreferencesRepository
    fun analytics(): AnalyticsClient
    fun countryFlags(): CountryFlagSource

    fun startScenes(): StartSceneSource
    fun taskContinuation(): TaskContinuationController

    /**
     * The two provider seams the Chromium platform adapter answers through
     * (decision 0081): the coordinator holds every credential mutation and
     * the freshness decision inside its per-provider sections, and the
     * engine folds browser-reported sign-in events into the visible state.
     * Accessors rather than adapter constructor parameters, because the
     * adapter is built by the shell after this graph exists and these are
     * the graph's own profile-scoped singletons.
     */
    fun providerCoordinator(): ProviderCredentialCoordinator
    fun providerSignInEngine(): ProviderSignInEngine

    /**
     * The product's own downloads, started with the profile rather than with
     * a screen.
     *
     * A binding nothing asks for is a binding Dagger never builds, and this
     * one has to exist before the first window does — see
     * [RequiredPartsInstaller]. The host starts it through
     * [startRequiredParts] rather than through this accessor, so that asking
     * for a download does not put the delivery module on the shell's own
     * classpath.
     */
    fun requiredParts(): RequiredPartsInstaller
    fun windowBuilder(): TaffyWindowComponent.Builder
    override fun tabBuilder(): TaffyTabComponent.Builder

    @Subcomponent.Builder
    interface Builder {
        @BindsInstance
        fun identity(identity: TaffyProfileIdentity): Builder

        @BindsInstance
        fun coreApiClient(client: CoreApiClient): Builder

        /** The value-only browser seam; absent from non-product graphs. */
        @BindsInstance
        fun taskInputEndpoint(endpoint: BrowserTaskInputEndpoint): Builder

        @BindsInstance
        fun preferenceStore(store: ProfilePreferenceStore): Builder

        @BindsInstance
        fun secureMaterial(store: AndroidProfileSecureMaterialStore): Builder

        @BindsInstance
        fun providerRevocation(port: ProviderRevocationPort): Builder

        /**
         * The manual-code fallback of decision 0095 section 2. Bound by the
         * shell like the revocation port, and for the same reason: only the
         * browser process can claim a redirect for a live flow.
         */
        @BindsInstance
        fun providerManualCode(port: ProviderManualCodePort): Builder

        fun build(): TaffyProfileComponent
    }
}

/**
 * Begin the downloads this product cannot work without.
 *
 * An extension rather than a call the host makes itself, because the host
 * would then name `RequiredPartsInstaller` and take a compile edge to the
 * delivery module for one method call. The Chromium shell's business here is
 * "the profile exists, start what a profile starts"; which component answers
 * that is this graph's business.
 */
fun TaffyProfileComponent.startRequiredParts() {
    requiredParts().start()
}

/** Start the regular profile's content-free task notification projection. */
fun TaffyProfileComponent.startTaskContinuation() {
    taskContinuation().start()
}
