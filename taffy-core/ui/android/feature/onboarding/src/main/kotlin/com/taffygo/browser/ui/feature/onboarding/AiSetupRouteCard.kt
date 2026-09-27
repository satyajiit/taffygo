// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One route choice on screen SCR-004.
 *
 * ## Unfilled until chosen
 *
 * Both cards used to carry a surface — raised when unchosen, sunken when
 * chosen — which meant fill was doing two jobs at once and neither well: every
 * card looked like an object, and the selected one looked like a pressed
 * button rather than a made choice. Here fill means exactly one thing. An
 * unchosen route is a hairline outline over the sequence's own ground, and
 * choosing one lifts it onto the raised surface behind a heavier border. That
 * is the start page's rule, and it is what lets a person see which answer they
 * gave from across the room.
 *
 * ## One block, not two
 *
 * The name, the sentence and the price used to be split across two stacked
 * rows: an icon-led heading, then a paragraph starting back at the card's own
 * left edge. Two left edges in a card 328 units wide read as two cards. The
 * glyph tile now heads one column that carries everything the route claims, so
 * the eye goes down a single edge and the price sits with the sentence that
 * explains it.
 *
 * ## Why there is no "See how Premium helps" here
 *
 * Mock 14 puts that chip inside the paid card, and it made the two cards
 * incomparable: at 48 units of touch target it added half a card's height to
 * one of the two answers, so the eye read a heavy option and a light one
 * rather than two options. It was also the only control inside a control —
 * a button in a card that is itself a radio button, which is a hit-target
 * problem as well as a reading one.
 *
 * SCR-710, the screen that chip opened, is deleted — TaffyGo sells nothing
 * (decision 0200). The paragraph above is kept because its reasoning is about
 * card weight and controls inside controls, which outlives the screen it was
 * written against. The claim that SCR-404's footer carried the same route was
 * never true: nothing outside this feature ever referenced that destination.
 * What this screen owes a person is the choice, stated once each way.
 */
@Composable
internal fun AiSetupRouteCard(
    option: AiSetupRoute,
    chosen: Boolean,
    onChoose: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(
                if (chosen) TaffyTheme.colors.surfaceRaised else Color.Transparent,
            )
            .border(
                width = if (chosen) TaffyBorders.emphasis else TaffyBorders.standard,
                color = if (chosen) TaffyTheme.colors.textPrimary else TaffyTheme.colors.outline,
                shape = TaffyTheme.shapes.card,
            )
            .clickable(enabled = enabled, onClick = onChoose)
            .padding(TaffyTheme.spacing.snug)
            .testTag("$AI_SETUP_ROUTE_TEST_TAG_PREFIX${option.route.label}")
            .semantics(mergeDescendants = true) {
                selected = chosen
                role = Role.RadioButton
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        // Top, not centre. The two sentences wrap to different line counts, so
        // the cards are never quite the same height, and centring would put
        // their glyphs and their selection marks at two different heights
        // beside titles that are level with each other.
        verticalAlignment = Alignment.Top,
    ) {
        RouteTile(option = option)
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            RouteTitleRow(option = option)
            Text(
                text = taffyString(option.body),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            CostPill(cost = option.cost)
        }
        ChosenMark(chosen = chosen)
    }
}

/** The route's glyph, on the quiet tile that heads its column. */
@Composable
private fun RouteTile(option: AiSetupRoute) {
    Box(
        modifier = Modifier
            .size(TileSize)
            .clip(TaffyTheme.shapes.row)
            .background(TaffyTheme.colors.surfaceSunken),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            imageVector = option.icon,
            contentDescription = null,
            tint = TaffyTheme.colors.textPrimary,
            modifier = Modifier.size(TileIconSize),
        )
    }
}

/** The route's name. */
@Composable
private fun RouteTitleRow(option: AiSetupRoute) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = TileSize),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = taffyString(option.title),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
    }
}

/**
 * Chosen draws the filled check; unchosen draws a hollow ring rather than an
 * empty box.
 *
 * A radio group with nothing where the unselected mark should be does not read
 * as "not this one", it reads as a card that is missing something — and the
 * empty box is what SCR-004 had.
 */
@Composable
private fun ChosenMark(chosen: Boolean) {
    Box(
        modifier = Modifier.heightIn(min = TileSize),
        contentAlignment = Alignment.Center,
    ) {
        if (chosen) {
            Icon(
                imageVector = TaffyIcon.CheckCircle,
                contentDescription = null,
                tint = TaffyTheme.colors.textPrimary,
                modifier = Modifier.size(ChosenMarkSize),
            )
        } else {
            Box(
                modifier = Modifier
                    .size(HollowMarkSize)
                    .border(
                        width = TaffyBorders.emphasis,
                        color = TaffyTheme.colors.outline,
                        shape = TaffyTheme.shapes.pill,
                    ),
            )
        }
    }
}

/**
 * The route's cost, neutral until the person chooses it.
 *
 * It keeps its sunken ground on an otherwise unfilled card, which is the point
 * of it: the price is the one fact on this screen a person may want to find
 * again without reading the sentence around it.
 */
@Composable
private fun CostPill(cost: Int) {
    Row(
        modifier = Modifier
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.surfaceSunken)
            .padding(
                horizontal = TaffyTheme.spacing.tight,
                vertical = TaffyTheme.spacing.step,
            ),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.Tag,
            contentDescription = null,
            tint = TaffyTheme.colors.textSecondary,
            modifier = Modifier.size(PillIconSize),
        )
        Text(
            text = taffyString(cost),
            style = TaffyTheme.typography.caption,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/** The tag screen SCR-004's semantics tests name on a route card. */

private val TileSize = 40.dp
private val HollowMarkSize = 20.dp
private val TileIconSize = 20.dp
private val ChosenMarkSize = 22.dp
private val PillIconSize = 12.dp
