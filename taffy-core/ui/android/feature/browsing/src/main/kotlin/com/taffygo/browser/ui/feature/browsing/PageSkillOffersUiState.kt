// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.SavedFlowReview
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartFailure

/** Ephemeral offers for the exact inspected page; opaque identities never enter a saved route. */
data class PageSkillOffersUiState(
    val open: Boolean = false,
    val host: String = "",
    val availability: Availability = Availability.LOADING,
    val offers: List<Offer> = emptyList(),
    val starting: Boolean = false,
    val reviewLoading: Boolean = false,
    val failure: TaskStartFailure? = null,
) {
    data class Offer(
        val id: String,
        val skillId: String,
        val version: UInt,
        val stepCount: UInt,
        val review: SavedFlowReview?,
        val start: TaskStartDecision,
    )

    enum class Availability { LOADING, READY, EMPTY, STALE, REVIEW_UNAVAILABLE, UNAVAILABLE }
}
