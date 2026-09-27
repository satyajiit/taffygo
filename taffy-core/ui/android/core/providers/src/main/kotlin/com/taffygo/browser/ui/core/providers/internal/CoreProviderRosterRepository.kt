// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providers.internal

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.common.Clock
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CoreStatus
import taffy.core_api.ProviderRosterEntry

/**
 * Profile-owned projection over the immutable Core API roster.
 *
 * One thing is added on this side of the projection and it is a reading,
 * not a fact the core lacks: the wall-clock moment this profile first saw a
 * given refusal. The core's `at_monotonic_ms` is exact and cannot be shown
 * as a time, so the moment is stamped here, once per distinct monotonic
 * reading, and dropped the moment the refusal clears.
 */
internal class CoreProviderRosterRepository(
    core: CoreApiClient,
    lifetime: TaffyProfileLifetime,
    private val clock: Clock,
) : ProviderRosterRepository {

    /**
     * Per provider, the monotonic reading last stamped and the stamp. Touched
     * only from the one collector below, in publication order, so a plain map
     * is enough; the initial value is computed before that collector starts.
     */
    private val observed = mutableMapOf<String, Pair<ULong, Long>>()

    override val roster: StateFlow<ProviderRosterState> = core.status
        .distinctUntilChangedBy(CoreStatus::providerRosterProjectionVersion)
        .map { it.toRosterState().stamped() }
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.toRosterState().stamped(),
        )

    override val models: StateFlow<Map<String, List<ProviderModel>>> = core.status
        .distinctUntilChangedBy(CoreStatus::providerModelsProjectionVersion)
        .map { it.toModelsByProvider() }
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.toModelsByProvider(),
        )

    /**
     * Stamp each standing refusal with when this profile first saw it.
     *
     * The stamp follows the core's monotonic reading rather than the snapshot:
     * a roster republished because something else changed carries the same
     * refusal with the same reading, and keeps the same stamp, so "a few
     * minutes ago" does not reset to "just now" every time the core speaks. A
     * new reading is a new refusal and is stamped afresh; a row that no
     * longer refuses forgets its stamp, so the next refusal is not dated from
     * the last one.
     */
    private fun ProviderRosterState.stamped(): ProviderRosterState {
        val standing = rows.mapNotNull { row -> row.lastRefusal?.let { row.providerId } }.toSet()
        observed.keys.retainAll(standing)
        return copy(rows = rows.map { it.stampRefusal() })
    }

    private fun ProviderRosterRow.stampRefusal(): ProviderRosterRow {
        val refusal = lastRefusal ?: return this
        val previous = observed[providerId]
        val seenAt = if (previous?.first == refusal.atMonotonicMs) {
            previous.second
        } else {
            clock.nowEpochMillis().also { observed[providerId] = refusal.atMonotonicMs to it }
        }
        return copy(lastRefusal = refusal.copy(observedAtEpochMillis = seenAt))
    }
}

/** Exact generated facts consumed by [CoreStatus.toRosterState]. */
internal data class ProviderRosterProjectionVersion(
    val ready: Boolean,
    val rows: List<ProviderRosterEntry>,
)

internal fun CoreStatus.providerRosterProjectionVersion() = ProviderRosterProjectionVersion(
    ready = hasCompleteProjection(),
    rows = provider_roster.takeIf { hasCompleteProjection() }.orEmpty(),
)

internal data class ProviderModelsProjectionVersion(
    val ready: Boolean,
    val rows: List<taffy.core_api.ProviderModelView>,
)

internal fun CoreStatus.providerModelsProjectionVersion() = ProviderModelsProjectionVersion(
    ready = hasCompleteProjection(),
    rows = provider_models.takeIf { hasCompleteProjection() }.orEmpty(),
)
