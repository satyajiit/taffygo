// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * How a fact came to be, from UX spec section 6: every fact is labelled by
 * kind and never presented as equally authoritative. A user's correction is
 * recorded alongside what the page said, never over it.
 */
enum class FactKind(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Read directly from the page. */
    FROM_THE_PAGE("from_the_page"),

    /** Condensed from what the page said. */
    SUMMARIZED("summarized"),

    /** Taffy's own reading of the evidence, not a claim the page made. */
    TAFFY_INFERENCE("taffy_inference"),

    /** The user typed it. */
    YOU_ENTERED("you_entered"),
}
