// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One cell of a workspace output (UX spec section 6). A fact always knows
 * where it came from: [sources] is never empty for a fact read from a page,
 * and a fact with no source is a defect rather than a confident answer.
 */
data class Fact(
    /** Identity of this fact within its workspace. */
    val id: FactId,
    /** Which output field this fact fills, as a string resource key name. */
    val field: String,
    /** The value as it will be shown. */
    val value: String,
    /** How the value came to be. */
    val kind: FactKind,
    /** Every source that supports the value. */
    val sources: List<SourceId>,
    /** A value the user entered, kept alongside rather than over [value]. */
    val correction: String? = null,
    /** Whether two sources disagree about this fact. */
    val hasConflict: Boolean = false,
    /** Whether the value's only sources have been excluded from scope. */
    val needsANewSource: Boolean = false,
)
