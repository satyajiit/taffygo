// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ThinkingLevel

/** Everything screen SCR-417 can be asked to do. */
sealed interface ModelSelectionIntent {

    /** Narrow the list to what was typed. Empty text is the whole list again. */
    data class Search(val query: String) : ModelSelectionIntent

    /** Show or hide the models behind providers that still need setting up. */
    data object ToggleLocked : ModelSelectionIntent

    /**
     * Pin this row's model for its provider.
     *
     * The row travels rather than a pair of identifiers, so the command that
     * follows can state the thinking level that should stand with it without
     * the screen having to look anything up a second time.
     */
    data class ChooseModel(val row: ModelSelectionUiState.Row) : ModelSelectionIntent

    /**
     * Ask this row's provider for this much thinking.
     *
     * [level] is null for **Auto**, which is Taffy deciding and is not
     * [ThinkingLevel.OFF]. The row travels for the same reason as above: the
     * model has to be named again or the same command would clear it.
     */
    data class ChooseThinking(
        val row: ModelSelectionUiState.Row,
        val level: ThinkingLevel?,
    ) : ModelSelectionIntent

    /**
     * Go to the page where this provider is set up.
     *
     * A locked block's rows send this instead of [ChooseModel]. Pinning a model
     * on a provider this browser cannot reach would write a choice with nothing
     * behind it, so the press goes where the missing thing can actually be
     * supplied.
     */
    data class OpenProvider(val block: ModelSelectionUiState.Block) : ModelSelectionIntent
}
