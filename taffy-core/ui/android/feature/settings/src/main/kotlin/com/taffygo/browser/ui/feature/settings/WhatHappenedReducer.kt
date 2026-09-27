// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Opening a terminal workspace is navigation and does not mutate this view. */
internal fun reduceWhatHappened(
    state: WhatHappenedUiState,
    intent: WhatHappenedIntent,
): WhatHappenedUiState = when (intent) {
    is WhatHappenedIntent.OpenTask -> state
}

/** Projection from the latest-workspace port. Unavailable carries no stale rows. */
internal fun projectWhatHappened(
    snapshot: WhatHappenedRepository.Snapshot,
): WhatHappenedUiState {
    val days = if (snapshot.availability == YouSurfaceAvailability.READY) {
        groupLatestEvents(snapshot.events)
    } else {
        emptyList()
    }
    return WhatHappenedUiState(
        availability = snapshot.availability,
        days = days,
        blockedRequestsThisWeek = snapshot.blockedRequestsThisWeek,
    )
}

/**
 * Sort once and build the day runs in one pass.
 *
 * The old `groupBy().toSortedMap().map()` path retained a hash map, every
 * bucket, a tree map, and the final list at the same time for as many as 500
 * rows. Duplicate ids are malformed for a latest-per-workspace projection;
 * keeping the newest one also guarantees unique keys for the lazy surface.
 */
private fun groupLatestEvents(
    events: List<WhatHappenedRepository.Event>,
): List<WhatHappenedUiState.Day> {
    val ordered = events.sortedWith(
        compareByDescending<WhatHappenedRepository.Event> { it.epochDay }
            .thenByDescending { it.epochMillis }
            .thenBy { it.id },
    )
    val seen = HashSet<String>(ordered.size.coerceAtMost(MAX_EVENT_ID_SET_CAPACITY))
    val days = ArrayList<WhatHappenedUiState.Day>()
    var day: Long? = null
    var dayEvents = ArrayList<WhatHappenedRepository.Event>()
    for (event in ordered) {
        if (!seen.add(event.id)) continue
        if (day != event.epochDay) {
            if (day != null) days += WhatHappenedUiState.Day(day, dayEvents)
            day = event.epochDay
            dayEvents = ArrayList()
        }
        dayEvents += event
    }
    if (day != null) days += WhatHappenedUiState.Day(day, dayEvents)
    return days
}

private const val MAX_EVENT_ID_SET_CAPACITY = 512
