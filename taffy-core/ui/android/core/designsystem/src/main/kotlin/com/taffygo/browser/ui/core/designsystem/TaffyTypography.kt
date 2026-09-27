// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.runtime.Immutable
import androidx.compose.ui.text.TextStyle

/**
 * The semantic type roles shared by every Taffy-owned surface.
 *
 * Screens choose meaning rather than a raw size. Every role uses scaled pixels,
 * so Android's font-size preference still scales the complete hierarchy.
 * Supporting roles stay distinct from primary copy even when colour alone is
 * unavailable, and [micro] is reserved for short, non-essential metadata.
 */
@Immutable
data class TaffyTypography(
    /** Onboarding headlines and empty-state headers. */
    val display: TextStyle,
    /** Screen titles and workspace names. */
    val headline: TextStyle,
    /** Cards, sheet titles, section headers, and prominent action labels. */
    val title: TextStyle,
    /** Default running text and primary interface copy. */
    val body: TextStyle,
    /** Muted descriptions and helper copy beneath a primary line. */
    val detail: TextStyle,
    /** De-emphasized metadata such as hosts, timestamps, and counts. */
    val caption: TextStyle,
    /** The smallest short metadata; never prose or an interactive label. */
    val micro: TextStyle,
    /**
     * Short emphasized control words, badges, column heads, and state labels.
     * Longer secondary copy belongs to [detail] or [caption].
     */
    val label: TextStyle,
    /** Counts, prices, and comparison cells, in tabular figures. */
    val numeric: TextStyle,
)
