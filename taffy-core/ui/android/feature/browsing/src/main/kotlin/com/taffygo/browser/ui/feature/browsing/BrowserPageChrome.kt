// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The chrome over a page, at both edges of the window.
 *
 * The page area fills this box and the chrome is laid out over it, but the two
 * opaque bars are also *measured*: their heights are handed to
 * [BrowserPageArea], which shortens the engine's viewport by them.
 * [BrowserTopBar] is the top-aligned sibling — where you are, and the one
 * overflow control. This column is the bottom one: moving between pages, the
 * Assistant pill, and the bands and overlays that belong with them. Find in
 * page is composed here, above the action row — never as a transform on the
 * page surface.
 *
 * ## Two bars are measured, and only one of them is free
 *
 * The action row's height never changes, so subtracting it costs nothing: the
 * cost of measuring chrome is *changing* it. What it buys is the foot of a
 * page — the last line of a document, and anything a page anchors to the
 * bottom of its own window. A cookie banner and its two buttons were the case
 * that proved it: drawn under an opaque row, they could be read and not
 * pressed.
 *
 * The top bar's height is zero for as long as it is hidden, so subtracting it
 * *does* cost: the viewport grows when the bar goes and shrinks when it
 * returns, and the engine lays the document out again each time. That is
 * deliberate and it is decision 0119's trade — a site's own header, banner or
 * first heading sits exactly where that bar is, and an opaque bar over it is
 * content that can only be read by scrolling it away. The relayout happens at
 * most once per gesture, because hiding has one door.
 *
 * Everything else in this column stays a pure overlay and is measured by
 * nothing: the private band, the takeover band, a notice, find in page, the
 * form Taffy holds open. They come and go on their own occasions, and a
 * viewport that followed them would relayout the document on every one.
 *
 * The status-bar inset is consumed exactly once, and by whichever surface is
 * actually under the status bar. Where there is a top bar the bar takes it and
 * the page runs edge to edge underneath; on the start page and while the page
 * tools are still arriving there is no top bar, so this box keeps it, exactly
 * as it did before the chrome was split. The choice reads [hasTopBar] — the
 * content alone — rather than [showsTopBar], because an inset that came and
 * went with a scroll would resize the page area on every scroll.
 *
 * [taskWait] is the second slot, filled by the application shell exactly as
 * [assistantBar] is: the form Taffy is holding open belongs to another feature,
 * and features never depend on features.
 */
@Composable
internal fun BrowserPageColumn(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
    assistantBar: (@Composable () -> Unit)?,
    startComposer: StartPageComposerSlot,
    modifier: Modifier = Modifier,
    taskWait: @Composable () -> Unit = {},
) {
    Box(
        modifier = modifier.then(
            if (state.hasTopBar()) Modifier else Modifier.windowInsetsPadding(TaffyEdges.top),
        ),
    ) {
        var overlayHeightPx by remember { mutableIntStateOf(0) }
        var topBarHeightPx by remember { mutableIntStateOf(0) }
        var actionRowHeightPx by remember { mutableIntStateOf(0) }
        BrowserPageArea(
            state = state,
            onIntent = onIntent,
            startComposer = startComposer,
            overlayHeightPx = overlayHeightPx,
            // A bar that has scrolled away covers nothing, so it takes
            // nothing: this is the zero that gives the page the top of the
            // window back. The last height the bar reported is kept in the
            // state above rather than recomputed, so the frame it returns on
            // is the frame the viewport shrinks on.
            topOverlayHeightPx = if (state.showsTopBar()) topBarHeightPx else 0,
            // The row alone, measured without the navigation-bar inset: the
            // column below adds that outside this row, and the page area takes
            // it for itself from `TaffyEdges.page`. Handing the column's height
            // instead would count the navigation bar twice and leave a strip of
            // dead ground under every page. The top bar's height needs the
            // opposite correction and gets it there, because it is measured
            // from the top of the window and so already contains its inset.
            actionRowHeightPx = actionRowHeightPx,
            modifier = Modifier.fillMaxSize(),
        )
        if (state.showsTopBar()) {
            BrowserTopBar(
                state = state,
                onIntent = onIntent,
                modifier = Modifier
                    .align(Alignment.TopCenter)
                    .onSizeChanged { topBarHeightPx = it.height },
            )
        }
        val chromeOnPage = state.content == BrowserContent.PAGE
        val chromeEdge = TaffyTheme.colors.outline
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .onSizeChanged { overlayHeightPx = it.height }
                .then(
                    if (chromeOnPage) {
                        Modifier
                            .background(TaffyTheme.colors.surface)
                            .drawBehind {
                                drawRect(
                                    color = chromeEdge,
                                    size = Size(size.width, TaffyBorders.standard.toPx()),
                                )
                            }
                            .padding(top = TaffyTheme.spacing.step)
                    } else {
                        Modifier
                    },
                )
                // The navigation bar and not the keyboard, so the action row
                // stays at the bottom of the window and the keyboard covers
                // it. What in this stack has to clear the keyboard is lifted
                // below, one line above the row, rather than by moving the
                // whole column.
                .windowInsetsPadding(TaffyEdges.bottomBar),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (state.isPrivate) {
                PrivateTabBand()
            }
            // The takeover band stood here, above everything else in this
            // column, answering "whose page is this". It is gone, and nothing
            // replaces it: it carried a mode chip, a second status line and
            // Take over, and the assistant pill one row below carries all
            // three — the mode in what it announces, the line as its only
            // line, and Take over as a chip. Two surfaces saying the same
            // thing is two things to read and one of them stale (decision
            // 0141). The page's own hold, `TakeoverInputLock`, is a different
            // claim and stays where it is.
            taskWait()
            if (state.notice != null) {
                BrowserNoticeCard(
                    notice = state.notice,
                    onDismiss = { onIntent(BrowserMainIntent.DismissNotice) },
                )
            }
            if (state.findInPage.open) {
                FindInPageOverlay(
                    state = state.findInPage,
                    onIntent = { onIntent(it.toBrowserMainIntent()) },
                )
            }
            // A person typing into find in page, into the form Taffy is
            // holding open, or into the start page's own box sees their field
            // above the keyboard, and the row of buttons underneath does not
            // come up to meet them. [keyboardLiftAbove] holds the arithmetic
            // and the reasoning, and screen SCR-102's dock uses the same rule
            // over a row of its own.
            val keyboardLift = keyboardLiftAbove(actionRowHeightPx)
            if (keyboardLift > 0.dp) {
                Spacer(Modifier.height(keyboardLift))
            }
            // No gate. This row is the one thing on this screen that is always
            // up — see this file's own note, and `PageAppearance.topBarVisible`
            // for what does answer to the scroll.
            ChromeRow(
                modifier = Modifier.onSizeChanged { actionRowHeightPx = it.height },
            ) {
                // While Taffy is driving, back, forward and tabs are refused
                // anyway, so the row closes them and the pill stretches into
                // what they leave instead of the row being swapped for a
                // different one (decision 0141). It is a parameter rather than
                // a fourth row because the movement is the point: same row,
                // same slot, one gesture. The null check is not belt and braces
                // — on a two-pane window the pill is drawn in the other pane
                // and this column is handed none.
                val taskHoldsTheRow =
                    assistantBar != null && state.takeover.hasTask && !state.takeover.taskEnded
                when {
                    state.content == BrowserContent.PAGE ||
                        state.content == BrowserContent.FAILED
                    -> BrowserActionRow(
                        state = state,
                        onIntent = onIntent,
                        assistantBar = assistantBar ?: {},
                        collapsed = taskHoldsTheRow,
                    )
                    // A tab that has been nowhere and a task that has finished:
                    // the dock stays away, because the pill still has its last
                    // line to say, and Tabs comes back beside it (decision
                    // 0140, amending decision 0132 section 2).
                    assistantBar != null && state.takeover.hasTask ->
                        BrowserStartAssistantRow(
                            userTabCount = state.userTabCount,
                            taffyTabCount = state.taffyTabCount,
                            onOpenTabSwitcher = { onIntent(BrowserMainIntent.OpenTabSwitcher) },
                            assistantBar = assistantBar,
                            collapsed = taskHoldsTheRow,
                        )
                    else -> BrowserStartActionRow(
                        userTabCount = state.userTabCount,
                        taffyTabCount = state.taffyTabCount,
                        onOpenTabSwitcher = { onIntent(BrowserMainIntent.OpenTabSwitcher) },
                        onOpenDownloads = { onIntent(BrowserMainIntent.OpenDownloads) },
                        onOpenWorkspaces = { onIntent(BrowserMainIntent.OpenWorkspaces) },
                        onOpenOptions = { onIntent(BrowserMainIntent.OpenSettings) },
                    )
                }
            }
        }
    }
}

@Composable
private fun ChromeRow(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Box(
        modifier = modifier
            .taffyChromeWidth()
            // One step above and below, and the gutter at the sides.
            //
            // The bottom used to be the full screen gutter, and on a phone
            // that read as a band of dead ground between the row and the
            // navigation bar — which has its own inset outside this row, so
            // the two were adding up. Both bars are now a step from what is
            // next to them: the page above, the system's own bar below.
            .padding(
                start = TaffyTheme.spacing.screenMargin,
                top = TaffyTheme.spacing.step,
                end = TaffyTheme.spacing.screenMargin,
                bottom = TaffyTheme.spacing.step,
            ),
    ) {
        content()
    }
}

@Composable
internal fun BrowserAssistantSlot(
    assistantBar: @Composable () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .taffyChromeWidth()
            .padding(horizontal = TaffyTheme.spacing.screenMargin)
            .testTag(ASSISTANT_SLOT_TEST_TAG),
    ) {
        assistantBar()
    }
}

@Composable
private fun PrivateTabBand() {
    val title = taffyString(R.string.taffy_browser_private_title)
    val body = taffyString(R.string.taffy_browser_private_body)
    val description = taffyString(R.string.taffy_browser_private_description, title, body)
    Row(
        modifier = Modifier
            .taffyChromeWidth()
            .padding(horizontal = TaffyTheme.spacing.screenMargin)
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.privateTintWash)
            .border(TaffyBorders.standard, TaffyTheme.colors.privateTint, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.snug)
            .testTag(PRIVATE_TEST_TAG)
            .semantics(mergeDescendants = true) {
                contentDescription = description
                liveRegion = LiveRegionMode.Polite
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.EyeSlash,
            contentDescription = null,
            modifier = Modifier.size(PrivateGlyphSize),
            tint = TaffyTheme.colors.privateTint,
        )
        Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
            Text(
                text = title,
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = body,
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

/** The private-tab band, drawn only while the selected tab is a private one. */
const val PRIVATE_TEST_TAG: String = "browser_private"

/** The application-filled Assistant pill in the centre of the navigation row. */
const val ASSISTANT_SLOT_TEST_TAG: String = "browser_assistant_slot"

private val PrivateGlyphSize = 20.dp
