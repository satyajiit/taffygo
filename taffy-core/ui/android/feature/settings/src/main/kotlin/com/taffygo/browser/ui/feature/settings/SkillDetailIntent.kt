// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.SavedFlowReview

/** Everything screen SCR-602 can be asked to do. */
sealed interface SkillDetailIntent {
    data object LoadRecordedReview : SkillDetailIntent

    /** Turn this ability on or off. */
    data object Toggle : SkillDetailIntent

    /** Accept exactly the definition displayed in the ordered review. */
    data class AcceptRecorded(val review: SavedFlowReview) : SkillDetailIntent

    /** Remove this ability if it can be removed. */
    data object Remove : SkillDetailIntent
}
