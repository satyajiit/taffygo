// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

/** Internal transport only. The host never exposes [reviewToken] or this mutable array. */
data class NativeRestorePreparation(
    val reviewToken: Long,
    val targetProfileLabel: String,
    val flattenedClassCounts: IntArray,
    val hasConflicts: Boolean,
    val canStage: Boolean,
    val status: Int,
)
