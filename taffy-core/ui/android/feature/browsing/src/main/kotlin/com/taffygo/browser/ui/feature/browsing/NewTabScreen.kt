// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.screenViewModel

/** Screen SCR-102 — the start page. */
@Composable
fun NewTabScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    taskPanel: @Composable (
        started: StartedTask,
        onTryAgain: () -> Unit,
        onLeave: () -> Unit,
    ) -> Unit = { _, _, _ -> },
) {
    val viewModel: NewTabViewModel = screenViewModel(TaffyDestination.NewTab)
    val state by viewModel.state.collectAsStateWithLifecycle()
    val onIntent: (NewTabIntent) -> Unit = { viewModel.onIntent(it, navigator) }

    LaunchedEffect(Unit) { viewModel.onShown() }

    // This screen's own box, on this screen's own back-stack entry — never the
    // address bar's, which is a destination nothing here pushes any more. See
    // [rememberStartPageComposer] for why the entry has to be the host's.
    val composer = rememberStartPageComposer(
        host = TaffyDestination.NewTab,
        navigator = navigator,
        menuActions = StartPageMenuActions(openLibrary = { onIntent(NewTabIntent.OpenLibrary) }),
        showsStartBody = state.startPageGate.ready,
        onFirstFocus = { onIntent(NewTabIntent.ComposerFocused) },
        taskPanel = taskPanel,
    )

    NewTabContent(
        state = state,
        onIntent = onIntent,
        composer = composer,
        modifier = modifier,
    )
}

/**
 * The stateless half, which is what a preview and a semantics test render.
 *
 * ## The shape
 *
 * The body is [StartPageBody], built around its middle: the backdrop fills
 * the screen, the greeting floats over it, the one box sits at the centre,
 * and the person's own most-visited sites sit under the box as tiles. There
 * is no feed and nothing that could become one — the grid is the person's
 * visit counts, kept on this device.
 *
 * The **private-tab entry is gone from here**, and private browsing is not:
 * the way in is the tab switcher's own Private segment, whose add control
 * opens a private tab because that is the segment it is standing in.
 *
 * ## Why this frame is not [com.taffygo.browser.ui.core.ui.TaffyScreen]
 *
 * That frame paints the flat theme surface and lays its body out inside the
 * window insets — right for every settled screen, and wrong for the one
 * screen whose ground is weather: the backdrop would start below the status
 * bar and stop above the navigation bar, leaving two flat strips at exactly
 * the edges the wash exists to reach. So this screen owns its frame: the
 * backdrop is painted first, edge to edge, and only the content is pulled
 * back off the bars — the sides here, the top by the body slot, the bottom
 * by the footer, each consumed exactly once, which is the same arithmetic
 * the shared frame does.
 *
 * ## Why the action row is a footer, and the box is not
 *
 * The row is chrome, and `handoff/DESIGN.md` principle 5 keeps the action row
 * where the thumb already is. The address box used to be a second footer line;
 * it is the centre of the body now, because on a page that is mostly air the
 * box *is* the page, and the body it centres in scrolls internally so 200%
 * text never pushes the row off the screen.
 *
 * ## The keyboard lifts the body, not the dock
 *
 * The box is typed into where it stands (decision 0131), so this screen has a
 * keyboard on it for the first time — and the footer took the inset that
 * includes one, which would have carried the dock up to sit on top of the
 * keyboard. It takes the navigation bar alone now, and the gap between body and
 * footer is [keyboardLiftAbove]: the dock stays at the bottom of the window
 * with the keyboard drawn over it, the body shrinks to what is left, and the
 * box ends up above the keyboard rather than behind it. Screen SCR-101's chrome
 * column does the same arithmetic over its own row.
 *
 * The row is the shared four-slot dock — Downloads, Workspaces, Tabs,
 * Settings — and no assistant pill. Every slot is a plain destination, so the
 * gear goes straight to Settings, which is what its glyph says. The menu this
 * screen does have is on the box rather than in the row: the plus at the
 * field's leading edge, carrying the destinations the dock does not — see
 * [StartPageMenuActions] for what is behind it and why the dock's own two are
 * not.
 */
@Composable
fun NewTabContent(
    state: NewTabUiState,
    onIntent: (NewTabIntent) -> Unit,
    composer: StartPageComposerSlot,
    modifier: Modifier = Modifier,
) {
    var footerHeightPx by remember { mutableIntStateOf(0) }
    Box(modifier = modifier.fillMaxSize()) {
        StartPageBackdrop(
            modifier = Modifier
                .matchParentSize()
                .testTag(START_BACKDROP_TEST_TAG),
        )
        Column(
            modifier = Modifier
                .fillMaxSize()
                .windowInsetsPadding(TaffyEdges.sides)
                .testTag(TaffyDestination.NewTab.screenId),
        ) {
            // The body owns its height: the start page centres itself and
            // scrolls internally, and the preparing state is a centred column
            // of its own. A scrolling frame would hand both an unbounded
            // height, and nothing can be centred in a space with no middle.
            Box(
                modifier = Modifier
                    .weight(1f)
                    .windowInsetsPadding(TaffyEdges.top),
            ) {
                if (state.startPageGate.ready) {
                    StartPageBody(
                        frequent = state.frequent,
                        composer = composer,
                        onOpenSite = { onIntent(NewTabIntent.OpenSite(it)) },
                    )
                } else {
                    PreparingStartArea(
                        gate = state.startPageGate,
                        onRetry = { onIntent(NewTabIntent.RetryPageTools) },
                    )
                }
            }
            // Above the dock rather than around it, so the dock does not move.
            val lift = keyboardLiftAbove(footerHeightPx)
            if (lift > 0.dp) {
                Spacer(Modifier.height(lift))
            }
            // The shared frame's footer geometry, kept to the unit so this
            // row cannot drift from the rows that still sit in that frame.
            BrowserStartActionRow(
                userTabCount = state.userTabCount,
                taffyTabCount = state.taffyTabCount,
                onOpenTabSwitcher = { onIntent(NewTabIntent.OpenTabSwitcher) },
                onOpenDownloads = { onIntent(NewTabIntent.OpenDownloads) },
                onOpenWorkspaces = { onIntent(NewTabIntent.OpenWorkspaces) },
                onOpenOptions = { onIntent(NewTabIntent.OpenSettings) },
                modifier = Modifier
                    // The navigation bar and not the keyboard. Measured under
                    // that inset and over the padding, because the lift above
                    // is against the row's own block and adding the bar to it
                    // would count the bar twice.
                    .windowInsetsPadding(TaffyEdges.bottomBar)
                    .onSizeChanged { footerHeightPx = it.height }
                    .padding(
                        start = TaffyTheme.spacing.screenMargin,
                        top = TaffyTheme.spacing.snug,
                        end = TaffyTheme.spacing.screenMargin,
                        bottom = TaffyTheme.spacing.tight,
                    ),
            )
        }
    }
}
