// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyRadii
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.drawTaffyBorderComet
import com.taffygo.browser.ui.core.designsystem.taffyPhase
import com.taffygo.browser.ui.core.ui.TaffyBrandMark
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Ask Taffy about the tabs the person offered, or the one they are looking at.
 *
 * The dashed workspace row stays the "build a workspace" offer. This is the
 * question path: it opens the existing preview with those tabs as sources.
 * The border carries the start page's travelling light, and the trailing
 * mark is the product's own — the same two invitations, one shape.
 */
@Composable
internal fun TabSwitcherAskBar(
    state: TabSwitcherUiState,
    onIntent: (TabSwitcherIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (!state.canAskTaffy) return
    val count = state.askTaffyTabIds.size
    val label = taffyString(R.string.taffy_tab_switcher_ask_taffy)
    val description = taffyPlural(
        R.plurals.taffy_tab_switcher_ask_taffy_pages,
        count,
        count,
    )
    val still = TaffyTheme.reducedMotion
    val phase = taffyPhase(running = !still, periodMillis = CometPeriodMillis)
    // The one caller that does not take the mark's ramp. This bar's ground is
    // solid `accent`, and the ramp's last stop *is* `accent` — a light that
    // fades into the surface it is drawn on is a light with no tail. One dark
    // ink on amber, as a single-stop ramp.
    val comet = listOf(TaffyTheme.colors.accentOn)
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.row)
            .background(TaffyTheme.colors.accent)
            .border(TaffyBorders.standard, TaffyTheme.colors.accentOn, TaffyTheme.shapes.row)
            // The radius is passed because this is a row, not a pill: the
            // primitive's default is half the height, which on a 48 dp bar is a
            // 23 dp arc drawn round a 10 dp corner and then shaved flat by the
            // clip above. Read the token rather than restating the figure.
            .drawBehind {
                if (!still) {
                    drawTaffyBorderComet(
                        phase = phase.value,
                        ribbon = comet,
                        cornerRadius = CornerRadius(TaffyRadii.row.toPx()),
                    )
                }
            }
            .clickable(role = Role.Button) { onIntent(TabSwitcherIntent.AskTaffy) }
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .padding(start = TaffyTheme.spacing.snug, end = MarkPad)
            .testTag(ASK_TAFFY_TEST_TAG)
            .semantics(mergeDescendants = true) { contentDescription = description },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.accentOn,
            modifier = Modifier.weight(1f),
        )
        Box(
            modifier = Modifier
                .size(MarkDisc)
                .clip(TaffyTheme.shapes.pill)
                .background(TaffyTheme.colors.surfaceRaised),
            contentAlignment = Alignment.Center,
        ) {
            TaffyBrandMark(size = MarkSize, contentDescription = null)
        }
    }
}

const val ASK_TAFFY_TEST_TAG: String = "tab_switcher_ask_taffy"

private val MarkDisc = 36.dp
private val MarkSize = 28.dp
private val MarkPad = 6.dp

// One lap takes long enough to read as weather rather than as progress —
// the start page's own period, so the two invitations move alike.
private const val CometPeriodMillis = 3200
