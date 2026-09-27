// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** How much of one part of Taffy is on the device (screen SCR-203). */
enum class TaffyPartAvailability(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Nothing has been downloaded. */
    MISSING("missing"),

    /** Part of it is here, and a download can carry on from there. */
    PARTIAL("partial"),

    /** All of it arrived, and it is being checked. */
    CHECKING("checking"),

    /** It is in place and Taffy can use it. */
    READY("ready"),
}
