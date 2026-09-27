// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Range and open-site intents. Opening a site does not invent a measurement. */
internal fun reduceTimeOnSites(
    state: TimeOnSitesUiState,
    intent: TimeOnSitesIntent,
): TimeOnSitesUiState = when (intent) {
    is TimeOnSitesIntent.SelectRange -> state.copy(range = intent.range)
    is TimeOnSitesIntent.OpenSite -> state
}

/** Projection from the dwell-time port onto both ranges. Minutes stay measured. */
internal fun projectTimeOnSites(
    snapshot: TimeOnSitesRepository.Snapshot,
    range: TimeOnSitesUiState.Range,
): TimeOnSitesUiState {
    val ready = snapshot.availability == YouSurfaceAvailability.READY
    val today = measuredRange(snapshot.today)
    val week = measuredRange(snapshot.week)
    val chosen = when (range) {
        TimeOnSitesUiState.Range.TODAY -> today
        TimeOnSitesUiState.Range.THIS_WEEK -> week
    }
    return TimeOnSitesUiState(
        availability = snapshot.availability,
        range = range,
        sites = if (ready) chosen.sites else emptyList(),
        totalMillis = if (ready) chosen.totalMillis else 0L,
        largestMillis = if (ready) chosen.largestMillis else 0L,
        todayMillis = if (ready) today.totalMillis else 0L,
        weekMillis = if (ready) week.totalMillis else 0L,
        todaySiteCount = if (ready) today.sites.size else 0,
        weekSiteCount = if (ready) week.sites.size else 0,
        clearedWithHistory = snapshot.clearedWithHistory,
        hasGroupedSites = ready && snapshot.hasGroupedSites,
        recoveredFromCorruption = ready && snapshot.recoveredFromCorruption,
    )
}

private data class MeasuredRange(
    val sites: List<TimeOnSitesUiState.Site>,
    val totalMillis: Long,
    val largestMillis: Long,
)

private fun measuredRange(
    sites: List<TimeOnSitesRepository.Site>,
): MeasuredRange {
    val measured = ArrayList<TimeOnSitesUiState.Site>(sites.size)
    var totalMillis = 0L
    var largestMillis = 0L
    for (site in sites) {
        val durationMillis = site.durationMillis
        if (durationMillis <= 0L) continue
        measured += TimeOnSitesUiState.Site(
            site = site.site,
            durationMillis = durationMillis,
            grouped = site.grouped,
        )
        totalMillis = if (Long.MAX_VALUE - totalMillis < durationMillis) {
            Long.MAX_VALUE
        } else {
            totalMillis + durationMillis
        }
        largestMillis = maxOf(largestMillis, durationMillis)
    }
    measured.sortByDescending(TimeOnSitesUiState.Site::durationMillis)
    return MeasuredRange(
        sites = measured,
        totalMillis = totalMillis,
        largestMillis = largestMillis,
    )
}

/** Hours and whole minutes, never a guessed extra minute. */
internal fun timeOnSitesParts(durationMillis: Long): Pair<Long, Int> {
    val totalMinutes = durationMillis.coerceAtLeast(0L) / 60_000L
    return totalMinutes / 60L to (totalMinutes % 60L).toInt()
}

/** Width of a bar against the largest site in range, 0 when nothing is measured. */
internal fun timeOnSitesBarFraction(durationMillis: Long, largestMillis: Long): Float {
    if (largestMillis <= 0L || durationMillis <= 0L) return 0f
    return (durationMillis.toDouble() / largestMillis.toDouble())
        .coerceIn(0.0, 1.0)
        .toFloat()
}
