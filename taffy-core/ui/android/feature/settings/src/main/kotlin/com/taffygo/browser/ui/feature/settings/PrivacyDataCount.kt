// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** A bounded count that distinguishes absence of a fact from an exact zero. */
sealed interface PrivacyDataCount {
    data object Loading : PrivacyDataCount
    data object Unavailable : PrivacyDataCount

    data class Known(
        val value: Int,
        /** True when [value] is a safe lower bound rather than the whole source count. */
        val isLowerBound: Boolean = false,
    ) : PrivacyDataCount {
        init {
            require(value in 0..MAX_PRIVACY_PROJECTED_ITEMS)
            require(!isLowerBound || value > 0)
        }
    }
}

private const val MAX_PRIVACY_PROJECTED_ITEMS = 4_096
