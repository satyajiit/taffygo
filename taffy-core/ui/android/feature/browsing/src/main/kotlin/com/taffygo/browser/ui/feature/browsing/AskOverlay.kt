// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyOpacity
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyEaseOut
import com.taffygo.browser.ui.core.designsystem.taffyRunOnce
import com.taffygo.browser.ui.core.ui.TaffyBackHandler
import com.taffygo.browser.ui.core.ui.TaffyBrandMark
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyIconButton
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-301: Ask Taffy, over the page it is about (decision 0135).
 *
 * The page stays where it is and shows through a light scrim; the card at
 * the centre holds the start page's composer, and once a question has been
 * sent, Taffy's answer above it. It replaces the sheet that used to dock at
 * the bottom of the page: a sheet had half a window to say everything in and
 * offered its own expand and collapse to argue about the other half, while a
 * dialog over the page has the page visible for exactly as long as the
 * person is asking about it and closes the moment they are done.
 *
 * The shell draws it from `OverlayHost` over the whole window, in both
 * layouts, because the page it is about is the whole window's. Tapping the
 * scrim closes it; so does back, once the box is empty — the composer's own
 * back handler registers inside this one and takes the press first while
 * there are words to clear.
 *
 * @param conversation what stands above the box once a task has been started
 *   from here: the assistant feature's panel, filled by the shell, because
 *   browsing does not depend on the assistant. It is handed the started task
 *   and the two doors the box offers once that task has ended.
 */
@Composable
fun AskOverlay(
    destination: TaffyDestination.AssistantBar,
    navigator: TaffyNavigator,
    conversation: @Composable (
        started: StartedTask,
        onTryAgain: () -> Unit,
        onLeave: () -> Unit,
    ) -> Unit,
    modifier: Modifier = Modifier,
) {
    AskOverlayFrame(
        onDismiss = { navigator.goBack() },
        modifier = modifier.testTag(destination.screenId),
        scrollContent = false,
    ) {
        AskComposer(host = destination, navigator = navigator, conversation = conversation)
    }
}

/**
 * The stateless frame: scrim, back, and a centred card, which a preview and
 * a semantics test render around any content.
 *
 * The scrim is the theme's, at a fixed opacity rather than at the strength
 * the take-over band draws it: that band is asking the person to look at one
 * rectangle of the page, and this is asking them to look at the card while
 * the page stays readable behind it. The card takes every safe edge, the
 * keyboard included, so the box rises with the keyboard rather than under
 * it; it scrolls inside when a long answer outgrows the window, and the page
 * behind never does.
 */
@Composable
fun AskOverlayFrame(
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    scrollContent: Boolean = true,
    content: @Composable () -> Unit,
) {
    val reducedMotion = TaffyTheme.reducedMotion
    val entrance by taffyRunOnce(running = !reducedMotion, durationMillis = EntranceMillis)
    val alpha = if (reducedMotion) 1f else taffyEaseOut(entrance)
    val title = taffyString(R.string.taffy_ask_composer_title)
    val close = taffyString(R.string.taffy_ask_overlay_close)
    val colors = TaffyTheme.colors
    val shape = TaffyTheme.shapes.hero

    TaffyBackHandler(enabled = true, onBack = onDismiss)
    Box(
        modifier = modifier
            .fillMaxSize()
            .graphicsLayer { this.alpha = alpha },
    ) {
        // The scrim is a control, not decoration: it is named, so a screen
        // reader finds "Close Ask Taffy" where a sighted person taps the page.
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(colors.scrim.copy(alpha = TaffyOpacity.ASK_SCRIM))
                .semantics { contentDescription = close }
                .clickable(
                    interactionSource = null,
                    indication = null,
                    onClickLabel = close,
                    onClick = onDismiss,
                )
                .testTag(ASK_OVERLAY_SCRIM_TEST_TAG),
        )
        Box(
            modifier = Modifier
                .fillMaxSize()
                .windowInsetsPadding(TaffyEdges.all)
                .padding(TaffyTheme.spacing.screenMargin),
            contentAlignment = Alignment.Center,
        ) {
            Column(
                modifier = Modifier
                    .widthIn(max = CardMaxWidth)
                    .fillMaxWidth()
                    .shadow(24.dp, shape)
                    .clip(shape)
                    .background(colors.surfaceRaised)
                    .testTag(ASK_OVERLAY_CARD_TEST_TAG)
                    .semantics { paneTitle = title },
            ) {
                AskOverlayHeading(title = title, onDismiss = onDismiss)
                Column(
                    // `fill = false` so a short card is short: the weight caps
                    // the body at whatever the heading left, and takes no more
                    // than the content asks for. Only the body scrolls, so the
                    // card keeps its name in view however long the answer runs.
                    modifier = Modifier
                        .weight(1f, fill = false)
                        .then(if (scrollContent) Modifier.verticalScroll(rememberScrollState()) else Modifier)
                        .padding(
                            horizontal = TaffyTheme.spacing.screenMargin,
                            vertical = TaffyTheme.spacing.snug,
                        ),
                ) {
                    content()
                }
            }
        }
    }
}

/**
 * The card's name, its mark, and the way out that does not need a guess.
 *
 * A dialog over a page has to say what it is before it says anything else:
 * the page behind it is full of the site's own words, and a bare box of
 * controls standing on them belongs to nobody. The mark and two words fix
 * that in the least space it can be fixed in.
 *
 * The close control is not a duplicate of the scrim. The scrim is where a
 * person taps to dismiss and it is named for a screen reader that cannot see
 * it; this is a real control with a touch target, at the corner every dialog
 * on this platform puts one, for a person who does not know the page is still
 * there to tap.
 */
@Composable
private fun ColumnScope.AskOverlayHeading(title: String, onDismiss: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = TaffyTheme.spacing.screenMargin,
                end = TaffyTheme.spacing.tight,
                top = TaffyTheme.spacing.tight,
                bottom = TaffyTheme.spacing.tight,
            ),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TaffyGlyphFrame(size = 40.dp, color = TaffyTheme.colors.ribbonOneWash) {
            TaffyBrandMark(size = HeadingMarkSize, contentDescription = null)
        }
        Text(
            text = title,
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.weight(1f),
        )
        TaffyIconButton(
            icon = TaffyIcon.X,
            contentDescription = taffyString(R.string.taffy_ask_overlay_close_control),
            onClick = onDismiss,
            size = TaffyButtonSize.COMPACT,
            testTag = ASK_OVERLAY_CLOSE_TEST_TAG,
        )
    }
}

/** The tags the semantics tests name. */
const val ASK_OVERLAY_SCRIM_TEST_TAG: String = "ask_overlay_scrim"
const val ASK_OVERLAY_CARD_TEST_TAG: String = "ask_overlay_card"
const val ASK_OVERLAY_CLOSE_TEST_TAG: String = "ask_overlay_close"

/** A fade, short enough to read as arrival rather than as motion. */
private const val EntranceMillis = 180

/** Wide enough for an answer, narrow enough that a tablet still shows the page around it. */
private val CardMaxWidth = 560.dp

/** The mark beside the name: read as identity, not as an icon in a row. */
private val HeadingMarkSize = 24.dp
