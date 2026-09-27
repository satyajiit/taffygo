// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyTween
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The one action row at the bottom of the window (decision 0119).
 *
 * Back, Forward, the Assistant pill and Tabs, in that order — everything about
 * moving through pages, where the thumb is. The page-actions entry used to
 * close the row at its far edge; it is at the far edge of [BrowserTopBar] now,
 * beside the address it acts on, and there is still exactly one of it.
 * Destinations that do not belong in a slot live one tap behind that control —
 * see [BrowserMenu].
 *
 * **This row is drawn only over a page.** A tab that has been nowhere gets
 * [BrowserStartActionRow] instead.
 *
 * [collapsed] is a task driving this page. All three controls are refused while
 * that is true, so rather than swapping this row for a different one they close
 * in place and the pill stretches into what they leave — one row, one slot, one
 * movement (decision 0141). They are dropped from the composition once they have
 * finished closing, so a screen reader is never offered a control that is not
 * there, and they open again the moment the task ends.
 */
@Composable
internal fun BrowserActionRow(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
    modifier: Modifier = Modifier,
    assistantBar: @Composable () -> Unit = {},
    collapsed: Boolean = false,
) {
    val closing by taffyTween(
        target = if (collapsed) 1f else 0f,
        durationMillis = if (TaffyTheme.reducedMotion) 0 else RowCollapseMs,
    )
    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(ActionRowHeight)
            .testTag(ACTION_ROW_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(ActionGap),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        BrowserClosingSlot(closing) {
            BrowserChromeButton(
                icon = TaffyIcon.ArrowLeft,
                contentDescription = taffyString(R.string.taffy_browser_back),
                enabled = state.canGoBack,
                onClick = { onIntent(BrowserMainIntent.GoBack) },
                testTag = BACK_TEST_TAG,
            )
        }
        BrowserClosingSlot(closing) {
            BrowserChromeButton(
                icon = TaffyIcon.ArrowRight,
                contentDescription = taffyString(R.string.taffy_browser_forward),
                enabled = state.canGoForward,
                onClick = { onIntent(BrowserMainIntent.GoForward) },
                testTag = FORWARD_TEST_TAG,
            )
        }
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
                userTabCount = state.userTabCount,
                taffyTabCount = state.taffyTabCount,
                onClick = { onIntent(BrowserMainIntent.OpenTabSwitcher) },
            )
        }
    }
}

/**
 * One control slot, narrowing and fading to nothing rather than vanishing.
 *
 * The width is what makes the pill beside it stretch: the pill has the row's
 * remaining weight, so a slot giving its width up hands it straight over, and
 * the two movements are one movement. Gone entirely at the end of it, because a
 * zero-width control is still a control to everything that reads the tree.
 */
@Composable
internal fun BrowserClosingSlot(closing: Float, content: @Composable () -> Unit) {
    if (closing >= 1f) return
    Box(
        modifier = Modifier
            .width(ActionTarget * (1f - closing))
            .alpha(1f - closing)
            .clipToBounds(),
        contentAlignment = Alignment.Center,
    ) {
        content()
    }
}

/** Long enough to read as one movement, short enough not to delay the pill. */
private const val RowCollapseMs = 260

/** The tags the action row's semantics tests name. */
const val WORKSPACES_TEST_TAG: String = "browser_workspaces"
const val DOWNLOADS_TEST_TAG: String = "browser_downloads"
const val SETTINGS_TEST_TAG: String = "browser_settings"
const val BACK_TEST_TAG: String = "browser_back"

/** The stable forward-history target beside Back. */
const val FORWARD_TEST_TAG: String = "browser_forward"

/** The balanced warm-neutral navigation surface itself. */
const val ACTION_ROW_TEST_TAG: String = "browser_action_row"

/** The menu's reload tile, the site group's always-present member. */
const val RELOAD_TEST_TAG: String = "browser_menu_reload"

/** The same menu slot while the selected page is loading. */
const val STOP_LOADING_TEST_TAG: String = "browser_menu_stop_loading"
