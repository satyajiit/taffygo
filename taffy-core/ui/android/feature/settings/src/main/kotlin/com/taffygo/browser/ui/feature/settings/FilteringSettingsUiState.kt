// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/**
 * Screen SCR-206 — Ads and trackers.
 *
 * The week count is optional and never invented from [blockedTotal]. Null
 * means this week is not measured. Zero is a real zero.
 */
data class FilteringSettingsUiState(
    /** Whether blocking is on for this profile. */
    val enabled: Boolean = true,
    /** Requests blocked over the profile's lifetime. */
    val blockedTotal: Long = 0,
    /**
     * Requests blocked this week, when the plane counted a week. Private-tab
     * counts do not add to this. Null is not a guess from [blockedTotal].
     */
    val blockedThisWeek: Long? = null,
    /** Conservative lower bound on distinct sites in the week window. */
    val minimumSitesThisWeek: Int? = null,
    /**
     * Every host the person excepted. The order is the browser's own: the
     * filtering plane holds them in a sorted set and admits only lowercase
     * host characters, so what arrives is already byte-ordered.
     */
    val exceptionHosts: List<String> = emptyList(),
    /**
     * The one in-flight "Block again", and how it went.
     *
     * The seam answers whether it recorded the change and this screen used to
     * discard that answer, so a refusal looked like a row that simply had not
     * gone away yet.
     */
    val removal: Removal = Removal(),
) {
    /** Which host is being blocked again, and how that is going. */
    data class Removal(
        val host: String = "",
        val status: Status = Status.IDLE,
    ) {
        /**
         * The four states a removal can be in. `SUCCEEDED` is silent on this
         * screen: the row has left the list, which is the whole message.
         */
        enum class Status {
            IDLE,
            RUNNING,
            SUCCEEDED,
            FAILED,
        }
    }
}
