// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The letter a host is known by when the engine has no mark for it.
 *
 * Used by the start page's tiles and by screen SCR-104's cards: the same
 * host must wear the same face on both, and neither reaches the network
 * for a mark. The sweep is sampled from the 3D mark
 * (`brand/png/taffygo-mark-color-on-light.png`) — cobalt, violet, magenta,
 * coral — not the scheme's working amber and not a three-stop linear that
 * muddies to brown on cream. Brand hues stay local and are sampled from a
 * committed mark, never fetched.
 */
@Composable
internal fun SiteLetterAvatar(
    host: String,
    modifier: Modifier = Modifier,
    letterStyle: TextStyle = TaffyTheme.typography.title,
) {
    Box(
        modifier = modifier.background(MarkRibbon),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = siteInitial(host),
            style = letterStyle,
            color = MarkRibbonOn,
        )
    }
}

/**
 * The letter a host is known by when the engine has no mark for it.
 *
 * The prefix everyone types and nobody means is skipped, so `www.example.test`
 * and `example.test` wear the same initial. Deterministic — the same host
 * always draws the same tile — and honest: it is the site's own name, not an
 * identity invented for it.
 */
internal fun siteInitial(host: String): String =
    host.removePrefix("www.")
        .firstOrNull(Char::isLetterOrDigit)
        ?.uppercase()
        ?: "?"

private val MarkRibbon = Brush.sweepGradient(
    colors = listOf(
        Color(0xFF2F6BFF),
        Color(0xFF9B4DFF),
        Color(0xFFFF4B9A),
        Color(0xFFFF8A52),
        Color(0xFF2F6BFF),
    ),
)
private val MarkRibbonOn = Color.White
