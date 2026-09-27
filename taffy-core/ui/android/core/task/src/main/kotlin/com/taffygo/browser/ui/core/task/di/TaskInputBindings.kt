// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.di

import com.taffygo.browser.ui.core.api.BrowserTaskInputEndpoint
import com.taffygo.browser.ui.core.api.TaskInputClient
import com.taffygo.browser.ui.core.api.UnavailableTaskInputEndpoint
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileScope
import com.taffygo.browser.ui.core.task.TaskInputRepository
import com.taffygo.browser.ui.core.task.internal.CoreTaskInputRepository
import dagger.BindsOptionalOf
import dagger.Module
import dagger.Provides
import java.util.Optional

/**
 * Profile binding for the form seam, and the one place it is allowed to fail
 * closed.
 *
 * [BrowserTaskInputEndpoint] is optional on purpose. A Gradle build has no
 * browser process, so there is no vault to mint a field value in and nothing
 * that could spend one; the graph therefore resolves to
 * [UnavailableTaskInputEndpoint], every surface reads `isAvailable == false`,
 * and the build is truthful about what it has instead of offering to collect
 * something it would have to drop. A product build supplies the real endpoint
 * and the same code path picks it up — which is why this is an optional binding
 * rather than a hard-wired stand-in that a later change would have to remember
 * to remove.
 */
@Module
abstract class TaskInputBindings {
    /** Present in the product graph, absent in a Gradle one. */
    @BindsOptionalOf
    abstract fun browserTaskInputEndpoint(): BrowserTaskInputEndpoint

    companion object {
        @Provides
        @TaffyProfileScope
        fun provideTaskInputClient(
            endpoint: Optional<BrowserTaskInputEndpoint>,
        ): TaskInputClient = endpoint.orElseGet(::UnavailableTaskInputEndpoint)

        @Provides
        @TaffyProfileScope
        fun provideTaskInputRepository(
            client: TaskInputClient,
            lifetime: TaffyProfileLifetime,
        ): TaskInputRepository = CoreTaskInputRepository(client, lifetime)
    }
}
