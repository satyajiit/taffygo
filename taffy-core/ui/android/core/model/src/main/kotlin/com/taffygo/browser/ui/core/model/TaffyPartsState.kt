// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * Every part of Taffy on this device, and the connection fact that decides
 * whether any of them moves (screen SCR-203).
 *
 * The connection is held once rather than repeated on every part, because it
 * explains all of them at once: a screen showing nine parts waiting says
 * "waiting for a connection" once, not nine times.
 */
data class TaffyPartsState(
    /**
     * Whether the browser has described delivery to this layer even once.
     *
     * False is the value before any snapshot has arrived, and it is a
     * different fact from every other one here: [supported] `false` and an
     * empty [parts] are what a build with nothing published looks like, and
     * they are also what "we have not been told anything" looked like, so a
     * surface waiting on a part could not tell "still coming" from "never".
     * Screen SCR-102 waited on that ambiguity for the life of the process.
     *
     * Only the projection over a real delivery view sets this.
     */
    val answered: Boolean = false,
    /**
     * Whether this build of TaffyGo has parts to download at all.
     *
     * False is not a failure — it is a build with nothing published for it,
     * and saying so is what keeps an empty list from reading as a stall.
     * Read it together with [answered]: it means nothing until the browser
     * has spoken.
     */
    val supported: Boolean = false,
    /** What the connection costs. Offline is the only cost that holds a part back. */
    val connectionCost: TaffyConnectionCost = TaffyConnectionCost.OFFLINE,
    /** Every part, in catalog order. */
    val parts: List<TaffyPart> = emptyList(),
) {
    /** Whether there is anything to show. */
    val isEmpty: Boolean
        get() = parts.isEmpty()
}
