// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.FilteringSettings
import kotlinx.coroutines.flow.FlowCollector
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * This week's blocked-request count, when the plane has a week window.
 *
 * Null fields mean not measured. Zero is a real zero. Nothing here is derived
 * from the lifetime total, and private-tab counts do not add to the week.
 *
 * The browser plane records a seven-day window in preferences
 * (`taffy.filtering.week_*`). JNI getters live on `TaffyFilteringBridge`.
 * [BrowserBlockingWeekRepository] reads the week fields on
 * [FilteringSettings], which the browser seam fills from those getters.
 */
interface BlockingWeekRepository {

    val snapshot: StateFlow<Snapshot>

    /** A week window, or the honest absence of one. */
    data class Snapshot(
        val blockedThisWeek: Long? = null,
        val minimumSitesThisWeek: Int? = null,
    )
}

/** No week window on this phone. */
internal class UnavailableBlockingWeekRepository : BlockingWeekRepository {
    override val snapshot: StateFlow<BlockingWeekRepository.Snapshot> =
        MutableStateFlow(BlockingWeekRepository.Snapshot()).asStateFlow()
}

/** A measured week of nothing blocked. */
internal class EmptyBlockingWeekRepository : BlockingWeekRepository {
    override val snapshot: StateFlow<BlockingWeekRepository.Snapshot> =
        MutableStateFlow(
            BlockingWeekRepository.Snapshot(blockedThisWeek = 0, minimumSitesThisWeek = 0),
        ).asStateFlow()
}

/**
 * The week window as the browser seam publishes it on [FilteringSettings].
 *
 * Null fields stay not-counted. A live zero is not invented from the
 * lifetime total.
 */
internal class BrowserBlockingWeekRepository(
    filtering: StateFlow<FilteringSettings>,
) : BlockingWeekRepository {
    override val snapshot: StateFlow<BlockingWeekRepository.Snapshot> =
        MappedWeekSnapshot(filtering)
}

@OptIn(kotlinx.coroutines.ExperimentalForInheritanceCoroutinesApi::class)
private class MappedWeekSnapshot(
    private val filtering: StateFlow<FilteringSettings>,
) : StateFlow<BlockingWeekRepository.Snapshot> {

    override val replayCache: List<BlockingWeekRepository.Snapshot>
        get() = listOf(value)

    override val value: BlockingWeekRepository.Snapshot
        get() = snapshotOf(filtering.value)

    override suspend fun collect(
        collector: FlowCollector<BlockingWeekRepository.Snapshot>,
    ): Nothing {
        filtering.collect(
            object : FlowCollector<FilteringSettings> {
                override suspend fun emit(value: FilteringSettings) {
                    collector.emit(snapshotOf(value))
                }
            },
        )
    }
}

private fun snapshotOf(settings: FilteringSettings) = BlockingWeekRepository.Snapshot(
    blockedThisWeek = settings.blockedThisWeek,
    minimumSitesThisWeek = settings.minimumSitesThisWeek,
)
