// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.analytics

/**
 * Counts become buckets before they become parameters.
 *
 * An exact count is a small piece of information about a person's session; a
 * bucket is a fact about the product. The allowlist in data-and-privacy section
 * 14 permits the second and not the first.
 */
object AnalyticsBucket {

    /** The bucket [count] falls in. */
    fun of(count: Int): String = when {
        count <= 0 -> "NONE"
        count == 1 -> "ONE"
        count <= 5 -> "TWO_TO_FIVE"
        count <= 20 -> "SIX_TO_TWENTY"
        else -> "OVER_TWENTY"
    }

    /** Every bucket, so a test can walk them. */
    val ALL: List<String> = listOf("NONE", "ONE", "TWO_TO_FIVE", "SIX_TO_TWENTY", "OVER_TWENTY")
}
