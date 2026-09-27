// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.LocalPageSurface
import com.taffygo.browser.ui.core.ui.LocalPageSurfaceSlot
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPageSurface
import com.taffygo.browser.ui.core.ui.TaffySecureWindow
import com.taffygo.browser.ui.core.ui.screenViewModel

/**
 * Screen SCR-110 — one page the product opened for an errand.
 *
 * A vendor's sign-in, the page a key is fetched from, a vendor's own
 * documentation. It draws the page under a toolbar and nothing else: no address
 * bar, no action row, no tab switcher, and no entry in one.
 */
@Composable
fun ErrandPageScreen(
    destination: TaffyDestination.ErrandPage,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: ErrandPageViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    // The page a vendor is about to be handed a credential on stays out of
    // screenshots and out of the app switcher's preview, for as long as this
    // composition is in the tree and not one frame longer.
    TaffySecureWindow()

    // The errand ends when its screen goes, by whatever route it went. This is
    // the case that covers the ones nothing else does: an inbound link
    // replacing the whole back stack, a window torn down, a person leaving
    // through a path this screen never sees. Disposal is also the only moment
    // that still works — a view model is cleared after its own scope is
    // cancelled, so an errand ended there would never be ended at all.
    DisposableEffect(viewModel) {
        onDispose { viewModel.endErrand() }
    }

    // The page this route names is gone: process death brought the route back
    // without it, or something else closed it underneath. There is deliberately
    // nothing here to reopen it from — the address was never in the route — so
    // the honest thing is to leave.
    LaunchedEffect(state.gone) {
        if (state.gone) navigator.goBack()
    }

    BackHandler { viewModel.onIntent(ErrandPageIntent.Back, navigator) }

    ErrandPageContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        modifier = modifier,
    )
}

/**
 * The stateless half, which is what a preview and a semantics test render.
 *
 * ## The page is measured against the toolbar, and it is subtracted rather than slid
 *
 * Screen SCR-101 does the opposite — it slides the page down under its top bar
 * (`BrowserPageArea`) — and decision 0119 refuses subtracting there in as many
 * words. Read why it refuses it: "a document relaid out **on every hide**, a
 * black frame where the platform reallocated the page's surface, and a bar that
 * blinked because the resize came back through the scroll callbacks as
 * movement." Every one of those costs is a cost of *hiding*. This toolbar never
 * hides, so there is no hide to pay for, and subtracting a constant height is
 * what the action row already does — which the same record prices at nothing,
 * because "a bar that never moves cannot cause" a relayout.
 *
 * It also has to be a subtraction here. A slide puts the last band of the page
 * off the bottom of the screen and buys that back when the bar goes away. On a
 * bar that never goes away, that band would simply never come back — and on a
 * consent screen the last band is where the button is.
 *
 * ## The keyboard is in the bottom, and that is not optional
 *
 * `TaffyEdges.page` deliberately leaves the IME out, and `BrowserPageArea`
 * composes it back in for the reason `TaffyEdges` gives: a page that ignored
 * the keyboard "left a person typing behind one". This is the one surface in
 * the product that exists to show a sign-in form. It is the last place that
 * could afford to get this wrong.
 */
@Composable
fun ErrandPageContent(
    state: ErrandPageUiState,
    onIntent: (ErrandPageIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val surface = LocalPageSurface.current
    val slot = LocalPageSurfaceSlot.current
    // The window hosts the page, so this screen reports where it belongs rather
    // than composing it, and its own ground must not be painted over the
    // surface sitting behind it. See [PageSurfaceSlot].
    val hosted = slot != null && surface.isLive
    Box(
        modifier = modifier
            .fillMaxSize()
            .then(
                if (hosted) Modifier else Modifier.background(TaffyTheme.colors.surface),
            )
            .testTag(TaffyDestination.ErrandPage("").screenId),
    ) {
        var toolbarHeightPx by remember { mutableIntStateOf(0) }
        val density = LocalDensity.current
        val edges = TaffyEdges.page
        // A maximum, not a sum: the toolbar is measured from the top of the
        // window and so already contains the status-bar and cutout strip.
        // Adding them would count that strip twice — the defect
        // `BrowserPageArea` names at its own top term.
        val pageTopPx = maxOf(toolbarHeightPx, edges.getTop(density))
        val pageBottomPx = maxOf(
            edges.getBottom(density),
            TaffyEdges.keyboard.getBottom(density),
        )
        val viewport = with(density) {
            PaddingValues(top = pageTopPx.toDp(), bottom = pageBottomPx.toDp())
        }

        val claim = remember { Any() }
        if (hosted) {
            val hostedSlot = requireNotNull(slot)
            SideEffect {
                hostedSlot.place(
                    owner = claim,
                    topPx = pageTopPx,
                    bottomPx = pageBottomPx,
                    slidePx = 0,
                    // SCR-110 has no page-supplied ground of its own: its
                    // toolbar never hides, so the surface colour is what
                    // shows beside the page.
                    groundArgb = null,
                )
            }
            DisposableEffect(hostedSlot, claim) { onDispose { hostedSlot.park(claim) } }
        }

        val pageRect = Modifier
            .matchParentSize()
            .padding(viewport)
            .testTag(ERRAND_CONTENT_TEST_TAG)
        if (hosted) {
            // The tagged node stays at the page's exact size, so the test that
            // proves the page is measured below the toolbar still measures it.
            Box(modifier = pageRect)
        } else if (surface.isLive) {
            TaffyPageSurface(surface = surface, modifier = pageRect)
        }

        ErrandPageToolbar(
            state = state,
            onIntent = onIntent,
            modifier = Modifier
                .align(Alignment.TopCenter)
                .onSizeChanged { toolbarHeightPx = it.height },
        )
    }
}

/** The page itself, for the test that proves it is measured below the toolbar. */
const val ERRAND_CONTENT_TEST_TAG: String = "errand_content"
