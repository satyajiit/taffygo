// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.task.ProviderReadinessFacts
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import com.taffygo.browser.ui.core.task.taffyReadiness
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CatalogLayerView
import taffy.core_api.CoreStatus
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderOriginView

/**
 * Readiness projected from the core's status, the held handles and the chosen
 * route. Only the facts that move the verdict are compared, so a task
 * progressing or a library refreshing recomputes nothing downstream.
 */
internal class CoreTaffyReadinessRepository(
    core: CoreApiClient,
    heldCredentialProviderIds: StateFlow<Set<String>>,
    chosenRoute: StateFlow<ProviderRoute>,
    lifetime: TaffyProfileLifetime,
) : TaffyReadinessRepository {
    override val readiness: StateFlow<TaffyReadiness> = combine(
        core.status.map { it.toReadinessFacts() }.distinctUntilChanged(),
        heldCredentialProviderIds,
        chosenRoute,
    ) { facts, held, chosen ->
        taffyReadiness(facts.copy(heldCredentialProviderIds = held), chosen)
    }.stateIn(
        lifetime.scope,
        SharingStarted.Eagerly,
        taffyReadiness(
            core.status.value.toReadinessFacts()
                .copy(heldCredentialProviderIds = heldCredentialProviderIds.value),
            chosenRoute.value,
        ),
    )
}

/**
 * The roster and account facts of one status. Unknown until the projection is
 * complete; a disabled row counts for nothing, because a request never
 * reaches it. An own address is a custom provider or a catalog one the person
 * repointed, and it needs no key of ours.
 */
internal fun CoreStatus.toReadinessFacts(): ProviderReadinessFacts {
    if (!hasCompleteProjection()) return ProviderReadinessFacts.UNKNOWN
    val enabled = provider_roster.filter { it.enabled }
    return ProviderReadinessFacts(
        known = true,
        usableCredentialProviderIds = enabled
            .filter { it.stored?.state == ProviderCredentialStateView.USABLE }
            .map { it.provider_id }
            .toSet(),
        storedCredentialProviderIds = enabled
            .filter { it.stored != null }
            .map { it.provider_id }
            .toSet(),
        ownEndpointProviderIds = enabled
            .filter {
                it.origin == ProviderOriginView.CUSTOM ||
                    it.catalog_layer == CatalogLayerView.USER_OVERRIDE
            }
            .map { it.provider_id }
            .toSet(),
    )
}
