// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One row under a focused address bar (screen SCR-103). The interpretation the
 * row would commit to is carried with it, so the screen never re-derives it and
 * the two can never disagree.
 */
data class Suggestion(
    /** Stable identity for the list. */
    val id: String,
    /** What the row shows. */
    val title: String,
    /** What choosing the row would do. */
    val interpretation: AddressBarInterpretation,
    /** Why this row exists, so its visible and spoken label never guesses. */
    val source: Source = Source.READING,
    /** A host or other safe secondary label; never private/profile data in private mode. */
    val supportingText: String? = null,
) {
    enum class Source {
        READING,
        BROWSER_COMMAND,
        OPEN_TAB,
        BOOKMARK,
        HISTORY,
    }
}
