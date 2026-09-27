// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The three modes of UX spec section 3. The mode is task state, not a global
 * setting, and the labels are exact: You browse, Browse together, Taffy
 * browses. Their wording is a string resource; this type is their identity.
 */
enum class AssistantMode(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Default. Taffy does nothing until asked, and shows no mode chip. */
    YOU_BROWSE("you_browse"),

    /** The user accepted one bounded step; it ends back in the user's hands. */
    BROWSE_TOGETHER("browse_together"),

    /** Entered only from an accepted start. */
    TAFFY_BROWSES("taffy_browses"),
    ;

    /** Whether a mode chip is shown. The default mode shows none. */
    val showsChip: Boolean
        get() = this != YOU_BROWSE
}
