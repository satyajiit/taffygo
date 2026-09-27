// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The one interpretation typed input resolves to (UX spec section 5). One box,
 * four content readings plus an explicitly enumerated browser command, and the
 * reading is always shown before anything consequential runs.
 *
 * [GoTo] and [Search] run immediately, as in any browser. [AskTaffy] answers in
 * the Assistant bar without changing the mode. [TaskForTaffy] opens a preview
 * first and starts nothing until the user says start.
 */
sealed interface AddressBarInterpretation {

    /** The typed text this interpretation was resolved from. */
    val input: String

    /** Whether choosing this interpretation may start work the user must preview first. */
    val needsPreview: Boolean

    /** The input is a location. */
    data class GoTo(override val input: String, val host: String) : AddressBarInterpretation {
        override val needsPreview: Boolean = false
    }

    /** The input is a query for the search engine. */
    data class Search(override val input: String) : AddressBarInterpretation {
        override val needsPreview: Boolean = false
    }

    /** The input is a question about what is on screen. */
    data class AskTaffy(override val input: String) : AddressBarInterpretation {
        override val needsPreview: Boolean = false
    }

    /** The input asks for work across pages, so it starts a task where it was typed. */
    data class TaskForTaffy(
        override val input: String,
        val template: TaskTemplate,
    ) : AddressBarInterpretation {
        override val needsPreview: Boolean = true
    }

    /** Exact typed words that open one existing browser destination. */
    data class BrowserCommand(
        override val input: String,
        val command: AddressBarCommand,
    ) : AddressBarInterpretation {
        override val needsPreview: Boolean = false
    }
}
