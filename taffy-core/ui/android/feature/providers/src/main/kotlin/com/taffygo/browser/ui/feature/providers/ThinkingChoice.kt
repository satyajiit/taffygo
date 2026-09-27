// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ThinkingLevel

/**
 * The thinking control on screen SCR-417: what one model offers, and what this
 * person asked it for.
 *
 * [chosen] is null when nobody has asked for an amount, which the control draws
 * as **Auto** — Taffy deciding. That is a different fact from
 * [ThinkingLevel.OFF], which is a person asking for no thinking phase at all,
 * and the two must never collapse into each other: `ThinkingLevel` carries no
 * member for Taffy deciding precisely so that the absence stays an absence.
 *
 * [rungs] comes from the model's own catalog entry rather than from the
 * enumeration's membership, so `XHIGH` and `MAX` appear only where a model maps
 * them.
 */
data class ThinkingChoice(
    /** Exactly the rungs this model offers, ascending, as the catalog listed them. */
    val rungs: List<ThinkingLevel> = emptyList(),
    /** The rung this person asked for, null while Taffy decides. */
    val chosen: ThinkingLevel? = null,
) {
    /**
     * Whether there is a choice to draw at all.
     *
     * One rung beside Auto is a radio group with one option: it says a person
     * has a choice they do not have. Fewer than two rungs draws nothing.
     */
    val offered: Boolean get() = rungs.size >= 2

    /** Whether Taffy is deciding, which is the absence of [chosen] and never a rung. */
    val automatic: Boolean get() = chosen == null

    /** Whether [level] is the standing answer. Auto is `null`, and never [ThinkingLevel.OFF]. */
    fun stands(level: ThinkingLevel?): Boolean = chosen == level
}
