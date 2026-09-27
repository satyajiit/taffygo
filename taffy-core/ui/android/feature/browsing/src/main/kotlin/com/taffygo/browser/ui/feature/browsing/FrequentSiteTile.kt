// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The frequent-sites grid under the start page's input, and its tiles.
 *
 * Everything on it is somewhere the person has actually been — the tiles are
 * projections of the visit counts `FrequentSitesRepository` keeps on this
 * device, never a suggestion or a feed. A profile that has been nowhere gets
 * one quiet line saying so, in place of the grid rather than beside an empty
 * one.
 *
 * A tile's avatar is the engine's own mark for the site — an open tab's
 * favicon when there is one, else the profile's favicon database, which
 * remembers the mark after the tab closes and across restarts — and the
 * site's initial on the mark's own ribbon only when neither store has
 * ever seen the site. The letter is drawn rather than fetched: chrome does
 * not reach the network for a mark. The disc is a sweep of the 3D mark's
 * own hues — cobalt, violet, magenta, coral — not the scheme's working
 * amber and not a three-stop linear that muddies to brown on cream. Amber
 * means Taffy is busy, and a tile that has no favicon is not a running task.
 */
@Composable
internal fun StartPageFrequentSites(
    frequent: List<FrequentTile>,
    onOpenSite: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (frequent.isEmpty()) {
        Text(
            text = taffyString(R.string.taffy_start_frequent_empty),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            textAlign = TextAlign.Center,
            modifier = modifier
                .fillMaxWidth()
                .testTag(START_FREQUENT_EMPTY_TEST_TAG),
        )
        return
    }
    FlowRow(
        modifier = modifier
            .fillMaxWidth()
            .testTag(START_FREQUENT_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(
            TaffyTheme.spacing.tight,
            Alignment.CenterHorizontally,
        ),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        maxItemsInEachRow = TilesPerRow,
    ) {
        frequent.forEach { tile ->
            FrequentSiteTile(tile = tile, onClick = { onOpenSite(tile.host) })
        }
    }
}

@Composable
private fun FrequentSiteTile(tile: FrequentTile, onClick: () -> Unit) {
    val description = taffyString(
        R.string.taffy_start_frequent_description,
        tile.title,
        tile.host,
    )
    Column(
        modifier = Modifier
            .width(TileWidth)
            .clip(TaffyTheme.shapes.card)
            .clickable(onClick = onClick, role = Role.Button)
            .padding(vertical = TaffyTheme.spacing.step)
            .testTag("$FREQUENT_TEST_TAG_PREFIX${tile.host}")
            .semantics(mergeDescendants = true) { contentDescription = description },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
    ) {
        val favicon = tile.favicon
        Box(
            modifier = Modifier
                .size(AvatarSize)
                .clip(CircleShape)
                .then(
                    if (favicon != null) {
                        // A bitmap brings its own colours; the hairline gives a
                        // pale one an edge against the backdrop.
                        Modifier.border(
                            TaffyBorders.standard,
                            TaffyTheme.colors.outline,
                            CircleShape,
                        )
                    } else {
                        // No mark to show, so the tile wears the logo's own
                        // sweep instead of an empty ring — a letter on a
                        // plain outline read as a placeholder where this
                        // reads as an avatar. The wash needs no border: it
                        // is its own edge.
                        Modifier
                    },
                ),
            contentAlignment = Alignment.Center,
        ) {
            if (favicon != null) {
                Image(
                    bitmap = favicon.asImageBitmap(),
                    contentDescription = null,
                    // The mark is the avatar, not an icon inside one: it fills
                    // the circle and the parent's clip rounds it, the same
                    // framing every avatar on the platform has taught. Crop
                    // rather than Fit so a non-square mark covers the circle
                    // instead of leaving bars of background beside it.
                    contentScale = ContentScale.Crop,
                    modifier = Modifier.matchParentSize(),
                )
            } else {
                SiteLetterAvatar(
                    host = tile.host,
                    modifier = Modifier.matchParentSize(),
                )
            }
        }
        Text(
            text = tile.host,
            style = TaffyTheme.typography.micro,
            color = TaffyTheme.colors.textSecondary,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

/** The tags the start page's semantics tests name for the grid. */
const val START_FREQUENT_TEST_TAG: String = "start_frequent"
const val START_FREQUENT_EMPTY_TEST_TAG: String = "start_frequent_empty"
const val FREQUENT_TEST_TAG_PREFIX: String = "start_frequent_"

// A tile is one thumb-width; four sit comfortably inside 360 dp with the
// screen margins taken. The avatar is the action-row target size, so the
// grid's touch targets match the chrome under it; a favicon fills it edge
// to edge rather than sitting inside it at a smaller size.
private const val TilesPerRow = 4
private val TileWidth = 72.dp
private val AvatarSize = 48.dp
