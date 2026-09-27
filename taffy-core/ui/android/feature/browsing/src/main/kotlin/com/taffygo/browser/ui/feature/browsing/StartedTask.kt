// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * The task the box started, once the core has admitted it and said its name.
 *
 * Held by the box rather than read back off the task repository, because the
 * repository answers "the task the surfaces follow" and this is a narrower
 * fact: *this* box, on *this* screen, is the one that started it, and the
 * panel drawn under the box is about that task and no other. The goal is kept
 * beside the identity so the panel can show the person their own words while
 * the first publication is still on its way.
 */
data class StartedTask(
    /** The core's identity for the task. */
    val id: String,
    /** What the person asked for, as they typed or spoke it. */
    val goal: String,
)
