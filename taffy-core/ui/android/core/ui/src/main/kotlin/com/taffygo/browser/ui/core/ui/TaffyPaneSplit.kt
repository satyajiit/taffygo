// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * How a two-pane split weights its sides.
 *
 * The values are the proportions the handoff's tablet frames use
 * (`handoff/DESIGN.md` section 9): a list-detail pair, a wide argument pane,
 * and a content-plus-Taffy pair. A screen names one of these rather than
 * inventing a third ratio.
 */
enum class TaffyPaneSplit(
    /** The weight of the leading pane. */
    val primaryWeight: Float,
    /** The weight of the trailing pane. */
    val secondaryWeight: Float,
) {
    /** Equal columns, for a task's timeline beside its sources. */
    HALF(1f, 1f),

    /** A narrower list beside a wider detail, for settings and workspaces. */
    LIST_DETAIL(0.42f, 0.58f),

    /** A wide leading pane, for a case made beside a decision. */
    WIDE_PRIMARY(0.54f, 0.46f),

    /** The page beside Taffy's pane. */
    CONTENT_ASSISTANT(1.2f, 0.8f),
}
