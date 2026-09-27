// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.task.internal.CoreTaffyReadinessRepository
import com.taffygo.browser.ui.core.task.internal.toReadinessFacts
import kotlinx.coroutines.flow.StateFlow
import taffy.core_api.CoreStatus

/**
 * The roster facts one status carries, read by the same rule the readiness
 * verdict reads them by. Unknown until the projection is complete, and never
 * holding the browser's own handles, which only the repository is given.
 */
fun CoreStatus.providerReadinessFacts(): ProviderReadinessFacts = toReadinessFacts()

/** Profile-scoped port: whether Taffy can reach a provider, kept current. */
interface TaffyReadinessRepository {
    val readiness: StateFlow<TaffyReadiness>
}

/**
 * The one real implementation, over the core's status and two facts the shell
 * supplies as flows: the provider ids the browser holds a credential handle
 * for, and the route the person chose. They arrive as flows rather than as
 * ports so this module imports neither the credentials nor the preferences
 * module; the binding that joins them lives in the application graph.
 */
fun taffyReadinessRepository(
    core: CoreApiClient,
    heldCredentialProviderIds: StateFlow<Set<String>>,
    chosenRoute: StateFlow<ProviderRoute>,
    lifetime: TaffyProfileLifetime,
): TaffyReadinessRepository =
    CoreTaffyReadinessRepository(core, heldCredentialProviderIds, chosenRoute, lifetime)
