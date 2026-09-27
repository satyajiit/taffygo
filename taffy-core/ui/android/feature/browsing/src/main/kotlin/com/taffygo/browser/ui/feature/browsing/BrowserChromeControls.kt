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
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffySpacing
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyTween
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The controls both bottom action rows are built from, and one of the rows.
 *
 * There are two rows and they are chosen between by what the tab is showing —
 * a page has history to go back through and a blank tab does not — but the
 * pieces they are made of are the same pieces, at the same size, with the
 * same badge on the same corner. They were private to [BrowserActionRow] while only one row
 * existed. A second row copying them is how two rows start to drift: the badge
 * moves two pixels on one screen, or the glyph is a size larger on the other,
 * and nothing fails because each row is internally consistent.
 *
 * [BrowserStartActionRow] lives here rather than beside either screen for the
 * same reason. Two screens draw it — SCR-102, and SCR-101 on a tab that has
 * been nowhere — and a second copy of it is the drift this file exists to
 * prevent, one level up.
 *
 * [BrowserTabsButton] takes two counts rather than a `BrowserMainUiState`, and
 * that is what lets the start row use it at all — the start page has no
 * navigation state to hand it, only the same two numbers from the same tab
 * list.
 */
@Composable
internal fun BrowserChromeButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    testTag: String,
    enabled: Boolean = true,
    targetSize: Dp = ActionTarget,
) {
    Box(
        modifier = Modifier
            .size(targetSize)
            .clip(TaffyTheme.shapes.pill)
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .testTag(testTag),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            imageVector = icon,
            contentDescription = contentDescription,
            modifier = Modifier.size(ActionGlyph),
            tint = if (enabled) TaffyTheme.colors.textPrimary else TaffyTheme.colors.hairline,
        )
    }
}

/**
 * The tabs glyph with its count badge; the words ride the content description.
 *
 * The badge is shown only when more than one tab is open: a count of one is
 * the browser's idle state, not something to advertise. Spoken description
 * still names the tab control. The badge turns accent the moment Taffy owns
 * any tab, so a task's tabs are visible from the chrome alone without opening
 * the switcher.
 */
@Composable
internal fun BrowserTabsButton(
    userTabCount: Int,
    taffyTabCount: Int,
    onClick: () -> Unit,
    testTag: String = TABS_TEST_TAG,
) {
    val tabs = userTabCount + taffyTabCount
    val taffyOwnsTabs = taffyTabCount > 0
    Box {
        BrowserChromeButton(
            icon = TaffyIcon.Browsers,
            contentDescription = taffyPlural(R.plurals.taffy_browser_tabs, tabs, tabs),
            onClick = onClick,
            testTag = testTag,
        )
        if (tabs > 1) {
            Text(
                text = taffyCount(tabs),
                // The spec's badge type: 9sp at W600, figures kept tabular.
                style = TaffyTheme.typography.numeric.copy(
                    fontSize = BadgeTextSize,
                    fontWeight = FontWeight.W600,
                ),
                color = if (taffyOwnsTabs) TaffyTheme.colors.accentOn else TaffyTheme.colors.surface,
                maxLines = 1,
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    // Inset onto the glyph's shoulder rather than the touch
                    // target's corner. The target is invisible, so on the page
                    // row the difference never showed — but the start dock
                    // draws a pill flush around these targets, and a badge on
                    // the target's corner sits welded to that pill's border
                    // instead of floating over the tabs icon it counts.
                    .padding(top = BadgeInset, end = BadgeInset)
                    .defaultMinSize(minWidth = BadgeMinimum, minHeight = BadgeMinimum)
                    .clip(RoundedCornerShape(BadgeCorner))
                    .background(
                        if (taffyOwnsTabs) {
                            TaffyTheme.colors.accent
                        } else {
                            TaffyTheme.colors.textPrimary
                        },
                    )
                    .padding(horizontal = TaffyTheme.spacing.step)
                    .wrapContentSize(Alignment.Center)
                    // The count is already the button's spoken label.
                    .testTag(TABS_BADGE_TEST_TAG)
                    .clearAndSetSemantics { },
            )
        }
    }
}

/**
 * The action row a tab with no page draws.
 *
 * Screen SCR-102's row, and screen SCR-101's whenever the tab in front of the
 * person has been nowhere. Those two moments are the same moment — the start
 * page — and until this composable existed only one of them knew it: SCR-101
 * drew the page row over its own start content, so a new tab opened from the
 * switcher came with a Back and a Forward that could never do anything, and
 * the same screen reached through SCR-102 did not. One row, drawn from the
 * same controls the page row is drawn from, is what makes that impossible.
 *
 * Four slots, in one raised dock at the centre of the row: Downloads,
 * Workspaces, Tabs, Settings. The dock is the address pill's own family —
 * raised, with the outline — so the one row of chrome on the start page sits
 * on the backdrop the way the box above it does, whatever colour the wash
 * behind happens to be. Downloads and Workspaces were behind the gear's
 * sheet when the dock held two; giving each its own slot is what let the
 * gear mean Settings and nothing else, on both hosts. Each remaining
 * absence is still a decision:
 *
 * - **No Back and no Forward.** This tab has been nowhere, so both would be
 *   permanently disabled: two dead controls in the two positions nearest the
 *   thumb, on the one surface where nothing has any history at all.
 * - **No Share.** There is nothing on a blank tab to share, and a control that
 *   cannot do its job is worse than an absent one, because it looks like it
 *   can.
 * - **No Assistant slot.** The start page is the one box and where the
 *   person goes; the conversation starts once there is a page to have it
 *   about.
 */
@Composable
internal fun BrowserStartActionRow(
    userTabCount: Int,
    taffyTabCount: Int,
    onOpenTabSwitcher: () -> Unit,
    onOpenDownloads: () -> Unit,
    onOpenWorkspaces: () -> Unit,
    onOpenOptions: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(ActionRowHeight)
            .testTag(NEW_TAB_ACTION_ROW_TEST_TAG),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier
                .clip(TaffyTheme.shapes.pill)
                .background(TaffyTheme.colors.surfaceRaised)
                .border(
                    TaffyBorders.standard,
                    TaffyTheme.colors.outline,
                    TaffyTheme.shapes.pill,
                )
                .padding(horizontal = TaffyTheme.spacing.snug),
            horizontalArrangement = Arrangement.spacedBy(DockGap),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BrowserChromeButton(
                icon = TaffyIcon.DownloadSimple,
                contentDescription = taffyString(R.string.taffy_browser_downloads),
                onClick = onOpenDownloads,
                testTag = NEW_TAB_DOWNLOADS_TEST_TAG,
            )
            BrowserChromeButton(
                icon = TaffyIcon.SquaresFour,
                contentDescription = taffyString(R.string.taffy_new_tab_workspaces),
                onClick = onOpenWorkspaces,
                testTag = NEW_TAB_WORKSPACES_TEST_TAG,
            )
            BrowserTabsButton(
                userTabCount = userTabCount,
                taffyTabCount = taffyTabCount,
                onClick = onOpenTabSwitcher,
            )
            BrowserChromeButton(
                icon = TaffyIcon.Gear,
                contentDescription = taffyString(R.string.taffy_browser_settings),
                onClick = onOpenOptions,
                testTag = NEW_TAB_SETTINGS_TEST_TAG,
            )
        }
    }
}

/**
 * The row the start page draws while the assistant bar is speaking for a task.
 *
 * The dock gives way to the pill rather than standing beside it, because the
 * pill is what the bar becomes and the design's row is one pill wide (decision
 * 0140, which amends decision 0132 section 2 — the pill used to carry the phase
 * lines only once there was a page). Two of the dock's four slots would be dead
 * here anyway and the other two are one hop behind the one that stays:
 *
 * - **Tabs stays.** It is the only dock slot that is a door out of this
 *   surface, and while Taffy works it is the door to Taffy's own tabs — which
 *   is exactly when its badge turns accent to mark them. Everything the other
 *   three reach is behind the switcher and the page menu; Tabs is what makes
 *   that chain exist at all, and without it a blank tab with a task running has
 *   no way out but the system back gesture.
 * - **Downloads, Workspaces and Settings go**, and come back whole with the
 *   dock the moment the task ends.
 * - **Back and Forward are still absent**, for the reason
 *   [BrowserStartActionRow] gives: this tab has been nowhere.
 *
 * [collapsed] is that task still going rather than finished, and it closes Tabs
 * too: while Taffy is driving, the switcher opens onto tabs it is still opening
 * (decision 0141). It closes in place and the pill stretches into it, the same
 * one movement [BrowserActionRow] makes with its three, and opens again when the
 * task ends.
 */
@Composable
internal fun BrowserStartAssistantRow(
    userTabCount: Int,
    taffyTabCount: Int,
    onOpenTabSwitcher: () -> Unit,
    assistantBar: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    collapsed: Boolean = false,
) {
    val closing by taffyTween(
        target = if (collapsed) 1f else 0f,
        durationMillis = if (TaffyTheme.reducedMotion) 0 else StartRowCollapseMs,
    )
    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(ActionRowHeight)
            .testTag(NEW_TAB_ASSISTANT_ROW_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(ActionGap),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .weight(1f)
                .testTag(ASSISTANT_SLOT_TEST_TAG),
            contentAlignment = Alignment.Center,
        ) {
            assistantBar()
        }
        BrowserClosingSlot(closing) {
            BrowserTabsButton(
                userTabCount = userTabCount,
                taffyTabCount = taffyTabCount,
                onClick = onOpenTabSwitcher,
            )
        }
    }
}

/** The page row's own collapse, restated here rather than shared as a number. */
private const val StartRowCollapseMs = 260

/**
 * The tags the start row's semantics tests name.
 *
 * They keep their `new_tab` names now that SCR-101 draws the row too, because
 * what SCR-101 draws it over *is* the start page — see [BrowserStartArea],
 * which stopped calling that moment "nothing open in this tab" for the same
 * reason.
 */
const val NEW_TAB_ACTION_ROW_TEST_TAG: String = "new_tab_action_row"
const val NEW_TAB_DOWNLOADS_TEST_TAG: String = "new_tab_downloads"
const val NEW_TAB_WORKSPACES_TEST_TAG: String = "new_tab_workspaces"
const val NEW_TAB_SETTINGS_TEST_TAG: String = "new_tab_settings"

/** The row that replaces the dock while the bar speaks for a task. */
const val NEW_TAB_ASSISTANT_ROW_TEST_TAG: String = "new_tab_assistant_row"

/** The tag both action rows give their tabs control. */
const val TABS_TEST_TAG: String = "browser_tabs"

/** The count badge itself, absent when only one tab is open. */
const val TABS_BADGE_TEST_TAG: String = "browser_tabs_badge"

// The action row's geometry, shared so the two rows cannot disagree about it.
// The tab badge is the handoff's (min 15dp, corner radius 8, label at 9sp),
// inset from the target's corner far enough to clear the dock pill's border
// and overlap the 21dp glyph it counts.
private val BadgeMinimum = 15.dp
private val BadgeCorner = 8.dp
private val BadgeTextSize = 9.sp
private val BadgeInset = 5.dp
internal val ActionRowHeight = 64.dp

/**
 * The target every chrome control draws at, and the width budget of the pill
 * between them.
 *
 * It was 56, which is 8 more than the minimum on each of three controls, and
 * those 24 came out of the one thing in the row that has words in it: on a
 * 390 dp phone the assistant pill had 190 dp for a line the spec fixes at "Done
 * — 3 sources, 1 conflict", which wants about 171 of them once the leading
 * shape and the pill's own insets are paid. At the minimum the pill has 214 and
 * the line fits. The design's own minimum is 44; this stays at the 48 the
 * spacing scale calls the minimum touch target, so nothing is traded for it.
 */
internal val ActionTarget = TaffySpacing.Compact.minimumTouchTarget

/** The design document's action-row glyph, which it sets at 19–22. */
internal val ActionGlyph = 21.dp

/** The handoff sets the slots side by side; each one carries its own padding. */
internal val ActionGap = 0.dp

/** Inside the start row's dock, where two targets share one raised pill. */
private val DockGap = 10.dp
