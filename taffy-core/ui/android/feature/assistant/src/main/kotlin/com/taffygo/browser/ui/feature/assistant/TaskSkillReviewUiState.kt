// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.SavedFlowReview

/** An offer belongs to one completed task and one immutable recorded definition. */
data class TaskSkillReviewUiState(
    val taskId: String? = null,
    val skillId: String? = null,
    val version: UInt = 0u,
    val origin: String = "",
    val loading: Boolean = false,
    val review: SavedFlowReview? = null,
    val saved: Boolean = false,
    val submitting: Boolean = false,
    val showReview: Boolean = false,
    val failed: Boolean = false,
)
