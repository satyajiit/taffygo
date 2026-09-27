// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One model a surface may offer, as the merged catalog serves it.
 *
 * The core carries these flat across every provider and bounded as a whole, so
 * one connected account with a long catalog cannot cost every other provider
 * its place in the snapshot. Grouping them under the provider that carries them
 * is the reader's work, and the roster repository does it once.
 */
data class ProviderModel(
    /** The provider this model belongs to, matching a [ProviderRosterRow]. */
    val providerId: String,
    /** The model's identity, and what a preference names. */
    val modelId: String,
    /** The name to show. */
    val displayName: String,
    /** How much the model can be given at once, in tokens. */
    val contextWindow: ULong,
    /** How much it can answer with, in tokens. */
    val maxOutputTokens: ULong,
    /** Whether the model reasons before answering. */
    val reasoning: Boolean,
    /**
     * Whether the model can be handed the tool vocabulary. A model that cannot
     * is offerable for asking and not for task work.
     */
    val toolCalling: Boolean,
    /** What the catalog cataloged this model for, in catalog order. */
    val roles: List<ModelRole>,
    /** What the model accepts. */
    val inputModalities: List<ModelInputModality>,
    /**
     * Exactly the rungs this model offers, ascending. Fewer than two means the
     * choice is not the person's to make and no control is drawn for it.
     */
    val thinkingLevels: List<ThinkingLevel>,
)
