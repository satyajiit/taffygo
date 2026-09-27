// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The frame every screen sits in: the theme's surface, the screen margin, a
 * title bar (Back on its own row, then the title), and a scrolling body.
 *
 * The body scrolls by default because text scales to twice its size (parity row
 * PAR-A11Y-003) and a screen that only fits at the default scale is a screen
 * that loses its controls at the largest one.
 *
 * ## Where the system bars go
 *
 * The surface fills the window and reaches the glass. What is pulled back off
 * the bars is only the part a person reads or touches, and it happens in three
 * places rather than one:
 *
 * - the **sides** are taken by the frame, because a long-edge inset and a
 *   navigation bar in landscape affect the title and the body equally;
 * - the **top** is taken by the title bar, so the title clears the status bar
 *   while the surface behind it still runs to the top of the screen;
 * - the **bottom** is taken inside the scroll, which is what lets the body pass
 *   under the navigation bar while its last control still stops above it. That
 *   inset carries the keyboard too, so a field on any of these screens is
 *   pushed clear when the keyboard opens and released when it closes.
 *
 * Each of the three is consumed where it is applied, so no part of the window
 * is counted twice.
 */
@Composable
fun TaffyScreen(
    destination: TaffyDestination,
    title: String?,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    subtitle: String? = null,
    scrollable: Boolean = true,
    toolbarRight: (@Composable () -> Unit)? = null,
    header: (@Composable ColumnScope.() -> Unit)? = null,
    footer: (@Composable ColumnScope.() -> Unit)? = null,
    content: @Composable () -> Unit,
) {
    val scroll = rememberScrollState()

    // One screen, one meaning of back.
    //
    // [onBack] is what the Back row in the title bar does. The system back
    // gesture used to do something else: the navigation host handles it with a
    // bare `navigator.goBack()`, so any screen whose own back does more than pop
    // the stack was two different controls wearing one name. Screen SCR-103 is
    // where that showed: its up control sends `Dismiss`, which empties the
    // composer, while the gesture popped the screen without telling the view
    // model — so the draft outlived the box and the next thing typed joined onto
    // the end of it. Routing the gesture here fixes the class rather than that
    // one screen, and a screen that declares no [onBack] is left to the host's
    // handler exactly as before.
    //
    // [TaffyBackHandler] rather than `BackHandler`, because the owner has to be
    // read rather than assumed — these frames are composed in previews and in
    // tests that have no activity behind them — and that guard belongs in one
    // place instead of at each screen that remembers to write it.
    if (onBack != null) {
        TaffyBackHandler(enabled = true, onBack = onBack)
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(TaffyTheme.colors.surface)
            .windowInsetsPadding(TaffyEdges.sides)
            .testTag(destination.screenId),
    ) {
        // A null title is a screen with no bar at all, not a screen with a
        // blank one, and screen SCR-102 is the surface that rule is about: it
        // had a bar whose only control was an up arrow that did exactly what
        // the system gesture already does. This used to cite
        // `docs/design/ux-spec.md` section 2, which said nothing sits above
        // the page but the status bar the system draws; decision 0119 ended
        // that rule on the browsing surface, where the address is a top bar
        // now. A settled screen still earns a bar by having something to put
        // in it. Whichever of the three parts comes first takes the top inset,
        // so it is still consumed exactly once.
        if (title != null) {
            TaffyTopBar(
                title = title,
                subtitle = subtitle,
                onBack = onBack,
                toolbarRight = toolbarRight,
                modifier = Modifier.windowInsetsPadding(TaffyEdges.top),
            )
        }
        // A control that chooses what the body shows cannot scroll away with
        // the body it is choosing. This slot carries no `weight`, so it is
        // measured before the body claims the rest — the mocks' `flex:none`
        // row between the title and the scrolling region.
        if (header != null) {
            Column(
                modifier = Modifier
                    .then(
                        if (title == null) {
                            Modifier.windowInsetsPadding(TaffyEdges.top)
                        } else {
                            Modifier
                        },
                    )
                    .padding(
                        start = TaffyTheme.spacing.screenMargin,
                        top = TaffyTheme.spacing.snug,
                        end = TaffyTheme.spacing.screenMargin,
                    ),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                content = header,
            )
        }
        Column(
            modifier = Modifier
                .weight(1f)
                .then(
                    if (title == null && header == null) {
                        Modifier.windowInsetsPadding(TaffyEdges.top)
                    } else {
                        Modifier
                    },
                )
                .then(
                    if (scrollable) {
                        Modifier.verticalScroll(scroll)
                    } else {
                        Modifier.fillMaxSize()
                    },
                )
                .then(
                    // With no fixed footer, the last body control clears the
                    // navigation bar itself. A fixed footer takes that inset
                    // instead, so it is never counted twice.
                    if (footer == null) {
                        Modifier.windowInsetsPadding(TaffyEdges.bottom)
                    } else {
                        Modifier
                    },
                )
                .padding(
                    horizontal = TaffyTheme.spacing.screenMargin,
                    vertical = TaffyTheme.spacing.snug,
                ),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            content = { content() },
        )
        if (footer != null) {
            Column(
                modifier = Modifier
                    .windowInsetsPadding(TaffyEdges.bottom)
                    .padding(
                        start = TaffyTheme.spacing.screenMargin,
                        top = TaffyTheme.spacing.snug,
                        end = TaffyTheme.spacing.screenMargin,
                        bottom = TaffyTheme.spacing.tight,
                    ),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                content = footer,
            )
        }
    }
}

/**
 * The same trusted frame as [TaffyScreen], with a virtualized body for a
 * collection whose size is not a small compile-time constant.
 *
 * Keeping this seam here prevents feature screens from nesting a lazy list
 * inside the default eager scroll container. The title, fixed header, footer,
 * insets, and back behavior therefore remain identical to every other screen.
 */
@Composable
fun TaffyLazyScreen(
    destination: TaffyDestination,
    title: String?,
    modifier: Modifier = Modifier,
    listModifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    subtitle: String? = null,
    toolbarRight: (@Composable () -> Unit)? = null,
    header: (@Composable ColumnScope.() -> Unit)? = null,
    footer: (@Composable ColumnScope.() -> Unit)? = null,
    content: LazyListScope.() -> Unit,
) {
    TaffyScreen(
        destination = destination,
        title = title,
        modifier = modifier,
        onBack = onBack,
        subtitle = subtitle,
        scrollable = false,
        toolbarRight = toolbarRight,
        header = header,
        footer = footer,
    ) {
        LazyColumn(
            modifier = listModifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            content = content,
        )
    }
}

/**
 * The same trusted frame as [TaffyScreen], with a virtualized bento body.
 *
 * The body is a lazy grid whose column count comes from
 * [rememberTaffyBentoColumns] over the width the body has, so it folds to one
 * column at large text exactly as [TaffyBentoGrid] does. Items are added with
 * [taffyBentoItem] and [taffyBentoItems], which clamp a span to the line.
 * [gridModifier] is applied to the grid node itself, which is where a screen's
 * container test tag belongs: a semantics test scrolls a lazy grid by asking
 * that node for a tag, and a tag on any wrapper cannot scroll anything.
 */
@Composable
fun TaffyLazyGridScreen(
    destination: TaffyDestination,
    title: String?,
    modifier: Modifier = Modifier,
    gridModifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    subtitle: String? = null,
    toolbarRight: (@Composable () -> Unit)? = null,
    header: (@Composable ColumnScope.() -> Unit)? = null,
    footer: (@Composable ColumnScope.() -> Unit)? = null,
    content: LazyGridScope.() -> Unit,
) {
    TaffyScreen(
        destination = destination,
        title = title,
        modifier = modifier,
        onBack = onBack,
        subtitle = subtitle,
        scrollable = false,
        toolbarRight = toolbarRight,
        header = header,
        footer = footer,
    ) {
        BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
            LazyVerticalGrid(
                columns = GridCells.Fixed(rememberTaffyBentoColumns(maxWidth)),
                modifier = gridModifier.fillMaxSize(),
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                content = content,
            )
        }
    }
}
