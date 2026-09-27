// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.graphics.vector.ImageVector

/**
 * One labelled overflow group: a heading and the tiles under it.
 *
 * A group with a null [heading] is the diagnostic inspector row after More —
 * not a shipping group, and never given a heading of its own.
 */
internal data class BrowserMenuGroup(
    val heading: String?,
    val headingTestTag: String?,
    val tiles: List<BrowserMenuTile>,
)

/**
 * One overflow tile: a 48 dp orb, a visible label, and what pressing it does.
 *
 * What it does is a lambda rather than a `BrowserMainIntent`, because the same
 * tiles are now offered from two surfaces that do not share an intent type —
 * the browsing surface's own overflow, and the plus on the start page's box.
 * Naming one screen's intent here would have meant a second copy of this list
 * for the other, which is how two menus that open the same things start to
 * disagree about what they open.
 */
internal class BrowserMenuTile(
    val icon: ImageVector,
    val label: String,
    val onSelect: () -> Unit,
    val testTag: String,
    val enabled: Boolean = true,
)
