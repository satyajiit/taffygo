// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * How many cells a tile claims across a bento row.
 *
 * A span wider than the grid is clamped to it, so a [Two] tile on a folded
 * one-column grid is simply the row, and [Full] is the row on any grid.
 */
enum class TaffyBentoSpan(val cells: Int) {
    /** One cell: a destination, a stat, a fact. */
    One(1),

    /** Two cells: a wide tile that still leaves room beside it on a tablet. */
    Two(2),

    /** The whole row: a hero, a section bar, a card of rows, an empty state. */
    Full(Int.MAX_VALUE),
}
