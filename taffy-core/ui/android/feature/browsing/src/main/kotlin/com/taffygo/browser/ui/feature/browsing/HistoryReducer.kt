// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import java.time.Instant
import java.time.ZoneId

/**
 * What screen SCR-201 shows.
 *
 * Private visits and Taffy's working trail are dropped here as well as at
 * the store, so a leaky snapshot cannot become the person's History. Days
 * are local to [zone], newest first, and a blank title is left blank — the
 * row speaks the host instead.
 */
internal fun projectHistory(
    snapshot: HistorySnapshot,
    query: String,
    nowEpochMillis: Long,
    zone: ZoneId = ZoneId.systemDefault(),
): HistoryUiState {
    return when (snapshot) {
        HistorySnapshot.Loading -> HistoryUiState(
            query = query,
            availability = HistoryUiState.Availability.LOADING,
        )
        HistorySnapshot.Unavailable -> HistoryUiState(
            query = query,
            availability = HistoryUiState.Availability.UNAVAILABLE,
        )
        is HistorySnapshot.Ready -> {
            val trimmed = query.trim()
            val latestById = HashMap<String, HistoryVisit>(
                snapshot.visits.size.coerceAtMost(MAX_HISTORY_PROJECTION_CAPACITY),
            )
            for (visit in snapshot.visits) {
                if (visit.isPrivate || visit.isTaffyWorkingTrail) continue
                val prior = latestById[visit.id.value]
                if (prior == null || visit.visitedAtEpochMillis > prior.visitedAtEpochMillis) {
                    latestById[visit.id.value] = visit
                }
            }
            val matched = ArrayList<HistoryVisit>(
                latestById.size,
            )
            for (visit in latestById.values) {
                if (trimmed.isEmpty() ||
                    visit.title.contains(trimmed, ignoreCase = true) ||
                    visit.host.contains(trimmed, ignoreCase = true)
                ) {
                    matched += visit
                }
            }
            HistoryUiState(
                query = query,
                availability = HistoryUiState.Availability.READY,
                days = groupHistoryDays(matched, nowEpochMillis, zone),
                totalCount = latestById.size,
            )
        }
    }
}

/** Visits that belong to the person, not to a private tab or Taffy's task. */
internal fun List<HistoryVisit>.forPerson(): List<HistoryVisit> =
    filterNot { it.isPrivate || it.isTaffyWorkingTrail }

private fun groupHistoryDays(
    visits: List<HistoryVisit>,
    nowEpochMillis: Long,
    zone: ZoneId,
): List<HistoryDay> {
    val today = Instant.ofEpochMilli(nowEpochMillis).atZone(zone).toLocalDate()
    val yesterday = today.minusDays(1)
    val ordered = visits.sortedWith(
        compareByDescending<HistoryVisit> { it.visitedAtEpochMillis }
            .thenBy { it.id.value },
    )
    val days = ArrayList<HistoryDay>()
    var epochDay: Long? = null
    var dayVisits = ArrayList<HistoryVisit>()
    fun finishDay() {
        val finished = epochDay ?: return
        val kind = when (finished) {
            today.toEpochDay() -> HistoryDay.Kind.TODAY
            yesterday.toEpochDay() -> HistoryDay.Kind.YESTERDAY
            else -> HistoryDay.Kind.DATE
        }
        days += HistoryDay(kind = kind, epochDay = finished, visits = dayVisits)
    }
    for (visit in ordered) {
        val visitDay = Instant.ofEpochMilli(visit.visitedAtEpochMillis)
            .atZone(zone)
            .toLocalDate()
            .toEpochDay()
        if (epochDay != visitDay) {
            finishDay()
            epochDay = visitDay
            dayVisits = ArrayList()
        }
        dayVisits += visit
    }
    finishDay()
    return days
}

private const val MAX_HISTORY_PROJECTION_CAPACITY = 1_024
