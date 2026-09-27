// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-207 — ranges and data classes. Never includes workspaces. */
data class ClearBrowsingDataUiState(
    val range: Range = Range.LAST_HOUR,
    val classes: Set<DataClass> = DataClass.entries.toSet(),
    val supportedClasses: Set<DataClass> = DataClass.entries.toSet(),
    val available: Boolean = false,
    val confirming: Boolean = false,
    val submitting: Boolean = false,
    val failed: Boolean = false,
) {
    enum class Range { LAST_HOUR, LAST_DAY, LAST_WEEK, ALL_TIME }

    enum class DataClass { HISTORY, COOKIES, CACHED_FILES, TIME_ON_SITES }

    val canClear: Boolean
        get() = available && classes.isNotEmpty() && classes.all { it in supportedClasses } &&
            !submitting
}
