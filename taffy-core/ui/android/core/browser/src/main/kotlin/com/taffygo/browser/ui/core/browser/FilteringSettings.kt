// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * The profile's ad- and tracker-blocking configuration and its counts, as
 * screen SCR-206 shows them.
 *
 * Facts only: the master toggle as the preference holds it, every host the
 * person has excepted, the lifetime blocked total, and the seven-day window
 * when the seam published one. The per-page live count is not here — it is
 * a fact about the selected tab and rides [NavigationState] with the rest
 * of them. Bandwidth saved and time saved are not facts this plane has.
 */
data class FilteringSettings(
    /** Whether blocking is on for this profile. On by default. */
    val enabled: Boolean = true,
    /**
     * Every host with a recorded exception, byte-ordered. The browser holds
     * them in a sorted set and records only lowercase host characters, so the
     * order is stable across reads and reads as alphabetical. Nothing above
     * this seam sorts them again; a second ordering authority would be a
     * second answer.
     */
    val exceptionHosts: List<String> = emptyList(),
    /** Requests blocked over the profile's lifetime, including this session. */
    val blockedTotal: Long = 0,
    /**
     * Requests blocked in the current seven-day window. Null means the seam
     * has not published a window. Zero is a counted zero. Never derived from
     * [blockedTotal], and private-tab counts do not add to this.
     */
    val blockedThisWeek: Long? = null,
    /**
     * A lower bound on distinct document hosts in the week window. The
     * browser retains at most its bounded identity capacity, so this never
     * claims the exact count after that capacity is reached.
     */
    val minimumSitesThisWeek: Int? = null,
)
