// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

/** Internal transport only. A positive token is meaningful only for an actionable status. */
data class NativeRestoreDiscovery(
    val recoveredReviewToken: Long,
    val targetProfileLabel: String,
    val flattenedClassCounts: IntArray,
    val hasConflicts: Boolean,
    val canStage: Boolean,
    val cleanupOnly: Boolean,
    val status: Int,
)
