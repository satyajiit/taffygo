// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Foreground time on sites, this profile, this phone.
 *
 * Private tabs, Taffy's tabs, the start page, and time the app was not in
 * front are never in a snapshot. The shipping adapter measures with a
 * monotonic clock and persists bounded intervals in the regular profile;
 * unavailable adapters still answer honestly and never draw a guessed bar.
 */
interface TimeOnSitesRepository {

    /** Today and this week, or an honest unavailable. */
    val snapshot: StateFlow<Snapshot>

    /** One complete answer from the port. */
    data class Snapshot(
        val availability: YouSurfaceAvailability,
        val today: List<Site> = emptyList(),
        val week: List<Site> = emptyList(),
        val clearedWithHistory: Boolean = false,
        val hasGroupedSites: Boolean = false,
        val recoveredFromCorruption: Boolean = false,
    )

    /** Time on one registrable domain, or an explicit bounded aggregate. */
    data class Site(
        val site: String,
        val durationMillis: Long,
        val grouped: Boolean = false,
    )
}

/** No dwell-time adapter was bound for this surface. */
internal class UnavailableTimeOnSitesRepository : TimeOnSitesRepository {
    override val snapshot: StateFlow<TimeOnSitesRepository.Snapshot> =
        MutableStateFlow(
            TimeOnSitesRepository.Snapshot(
                availability = YouSurfaceAvailability.UNAVAILABLE,
            ),
        ).asStateFlow()
}
