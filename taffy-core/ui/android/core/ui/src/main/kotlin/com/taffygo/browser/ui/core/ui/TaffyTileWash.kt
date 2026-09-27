// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.graphics.Color
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The wash behind a glyph, and — on a bento tile — the ground of the tile.
 *
 * Every value is a token. The three ribbon cases are the mark's own hues,
 * lent to a hub group by decision 0103: a hub tile's ground is the ribbon
 * wash, its well the lightened solid, and its words the ordinary ink, because
 * a ribbon hue never carries text. [inkColor] therefore answers `textPrimary`
 * for a ribbon case, so a section bar washed in a ribbon hue draws neutral
 * words rather than coral ones at 2.8:1.
 */
enum class TaffyTileWash {
    /** Recessed, no meaning. */
    Neutral,

    /** Taffy is in this destination. The only amber tile on a hub. */
    Accent,

    /** A good count, already true. */
    Positive,

    /** A private-tab destination. */
    Private,

    /** Something failed, or would be destructive. */
    Danger,

    /** The mark's first ribbon hue: the person's own group. */
    RibbonOne,

    /** The mark's second ribbon hue: this phone's group. */
    RibbonTwo,

    /** The mark's third ribbon hue: about and help. */
    RibbonThree,
}

/** The fill of the glyph host. */
@Composable
@ReadOnlyComposable
fun TaffyTileWash.washColor(): Color = when (this) {
    TaffyTileWash.Neutral -> TaffyTheme.colors.surfaceSunken
    TaffyTileWash.Accent -> TaffyTheme.colors.accentWash
    TaffyTileWash.Positive -> TaffyTheme.colors.positiveWash
    TaffyTileWash.Private -> TaffyTheme.colors.privateTintWash
    TaffyTileWash.Danger -> TaffyTheme.colors.dangerWash
    TaffyTileWash.RibbonOne -> TaffyTheme.colors.ribbonOneWash
    TaffyTileWash.RibbonTwo -> TaffyTheme.colors.ribbonTwoWash
    TaffyTileWash.RibbonThree -> TaffyTheme.colors.ribbonThreeWash
}

/** The ground of a bento tile, laid over `surfaceRaised`; nothing for Neutral. */
@Composable
@ReadOnlyComposable
fun TaffyTileWash.fillColor(): Color = when (this) {
    TaffyTileWash.Neutral -> Color.Transparent
    TaffyTileWash.Accent -> TaffyTheme.colors.accentWash
    TaffyTileWash.Positive -> TaffyTheme.colors.positiveWash
    TaffyTileWash.Private -> TaffyTheme.colors.privateTintWash
    TaffyTileWash.Danger -> TaffyTheme.colors.dangerWash
    TaffyTileWash.RibbonOne -> TaffyTheme.colors.ribbonOneWash
    TaffyTileWash.RibbonTwo -> TaffyTheme.colors.ribbonTwoWash
    TaffyTileWash.RibbonThree -> TaffyTheme.colors.ribbonThreeWash
}

/** The solid well a bento tile's glyph sits in. */
@Composable
@ReadOnlyComposable
fun TaffyTileWash.wellColor(): Color = when (this) {
    TaffyTileWash.Neutral -> TaffyTheme.colors.surfaceSunken
    TaffyTileWash.Accent -> TaffyTheme.colors.accentWell
    TaffyTileWash.Positive -> TaffyTheme.colors.positiveWash
    TaffyTileWash.Private -> TaffyTheme.colors.privateTintWash
    TaffyTileWash.Danger -> TaffyTheme.colors.dangerWash
    TaffyTileWash.RibbonOne -> TaffyTheme.colors.ribbonOneWell
    TaffyTileWash.RibbonTwo -> TaffyTheme.colors.ribbonTwoWell
    TaffyTileWash.RibbonThree -> TaffyTheme.colors.ribbonThreeWell
}

/** The glyph on that well. */
@Composable
@ReadOnlyComposable
fun TaffyTileWash.wellInk(): Color = when (this) {
    TaffyTileWash.Neutral -> TaffyTheme.colors.textSecondary
    TaffyTileWash.Accent -> TaffyTheme.colors.accentOn
    TaffyTileWash.Positive -> TaffyTheme.colors.positiveText
    TaffyTileWash.Private -> TaffyTheme.colors.privateTint
    TaffyTileWash.Danger -> TaffyTheme.colors.dangerText
    TaffyTileWash.RibbonOne,
    TaffyTileWash.RibbonTwo,
    TaffyTileWash.RibbonThree,
    -> TaffyTheme.colors.ribbonOn
}

/** The glyph and chapter index on that wash. */
@Composable
@ReadOnlyComposable
fun TaffyTileWash.inkColor(): Color = when (this) {
    TaffyTileWash.Neutral -> TaffyTheme.colors.textSecondary
    TaffyTileWash.Accent -> if (TaffyTheme.isDark) {
        TaffyTheme.colors.accentText
    } else {
        TaffyTheme.colors.accentDeep
    }
    TaffyTileWash.Positive -> TaffyTheme.colors.positiveText
    TaffyTileWash.Private -> TaffyTheme.colors.privateTint
    TaffyTileWash.Danger -> TaffyTheme.colors.dangerText
    TaffyTileWash.RibbonOne,
    TaffyTileWash.RibbonTwo,
    TaffyTileWash.RibbonThree,
    -> TaffyTheme.colors.textPrimary
}
