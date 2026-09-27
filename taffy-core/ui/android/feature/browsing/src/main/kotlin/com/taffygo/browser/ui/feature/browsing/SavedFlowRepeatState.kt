// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.SavedFlowReview

/** Only a submitted request can populate this transient review. */
data class SavedFlowRepeatState(
    val checking: Boolean = false,
    val reviews: List<SavedFlowReview> = emptyList(),
    val opening: Boolean = false,
    val failed: Boolean = false,
) {
    val visible: Boolean get() = checking || reviews.isNotEmpty() || opening || failed
}
