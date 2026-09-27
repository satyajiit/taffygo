// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The part of screen SCR-417 the screen itself owns.
 *
 * Everything else on the page is the core's published answer. This is what is
 * true only here and only now: what has been typed into the search field,
 * whether the providers that still need setting up are disclosed, and which
 * command was last sent.
 *
 * [asked] is deliberately not a selection. Nothing on the page is drawn as
 * chosen because of it — the roster's `selectedModelId` remains the only thing
 * that marks a model in use — so a core that refuses the command leaves the
 * page showing exactly what it showed before.
 */
data class ModelSelectionDraft(
    /** What has been typed into the search field. */
    val query: String = "",
    /** Whether the providers that still need setting up are showing their models. */
    val lockedExpanded: Boolean = false,
    /** The last command stated from this screen, null when none is outstanding. */
    val asked: ModelSelectionAsk? = null,
)
