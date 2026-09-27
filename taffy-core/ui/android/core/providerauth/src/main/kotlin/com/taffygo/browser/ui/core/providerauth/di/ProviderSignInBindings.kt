// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth.di

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.providerauth.ProviderManualCodePort
import com.taffygo.browser.ui.core.providerauth.ProviderSignInEngine
import com.taffygo.browser.ui.core.providerauth.ProviderSignInCommands
import dagger.Module
import dagger.Provides

/**
 * One sign-in engine per profile.
 *
 * Profile-scoped because the browser's flow events and the screens that
 * render them must fold into one state: a second engine would be a second
 * opinion about where a sign-in stands.
 *
 * Two seams feed the commands, and they are different on purpose. Starting
 * and cancelling are Core API commands, because admission is the core's
 * decision. A manually entered code is the browser broker's redirect claim
 * (decision 0095 section 2), so it goes through the platform port the shell
 * binds rather than through the core, which learns of it as an `EXCHANGING`
 * event exactly as it would of an intercepted redirect.
 */
@Module
object ProviderSignInBindings {

    @TaffyProfileScope
    @Provides
    fun engine(
        coreApi: CoreApiClient,
        manualCode: ProviderManualCodePort,
    ): ProviderSignInEngine =
        ProviderSignInEngine(
            object : ProviderSignInCommands {
                override suspend fun start(providerId: String): String {
                    check(coreApi.status.value.hasCompleteProjection()) {
                        "Core projection is unavailable"
                    }
                    return coreApi.startProviderAuth(providerId)
                }

                override suspend fun cancel(flowId: String) =
                    coreApi.cancelProviderAuth(flowId)

                override suspend fun submitCode(flowId: String, entered: String): Boolean =
                    manualCode.submit(flowId, entered)
            },
        )
}
