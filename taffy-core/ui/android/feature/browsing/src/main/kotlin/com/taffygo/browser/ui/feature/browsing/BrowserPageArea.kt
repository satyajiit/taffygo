// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.browser.PageSurface
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.LocalPageSurface
import com.taffygo.browser.ui.core.ui.LocalPageSurfaceSlot
import com.taffygo.browser.ui.core.ui.TaffyPageSurface
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The slot screen SCR-101 gives the web, and everything drawn in it instead.
 *
 * ## The page fills what is left, and nothing is drawn over it
 *
 * The engine's surface is the window less everything in front of it: the
 * system bars and the display cutout ([TaffyEdges.page], and deliberately not
 * the keyboard), the action row, which never moves, and the top bar, for
 * exactly as long as the top bar is up. Nothing here overlaps anything. A
 * document's first line is below the address pill, its last is above Back and
 * Forward, and neither is behind the clock or the gesture handle.
 *
 * ## The top bar moves the page; it does not resize it
 *
 * The surface's **size** is a constant: the window less the system bars, the
 * display cutout and the action row. It is the size the page has when the top
 * bar is away, and it does not change when the bar comes or goes. What changes
 * is where the surface starts — while the bar is up the surface is slid down
 * by the bar's height, so the document begins under the address pill instead
 * of behind it.
 *
 * **The keyboard is the one exception, and it has to be.** Nothing else the
 * engine is told about its window is allowed to move; the keyboard is, because
 * the engine is the only thing that knows where the field a person is typing
 * into actually is. Told that its window now ends at the top of the keyboard,
 * Blink scrolls that field into view by itself, which is what every browser
 * does. Left uninformed — which is what this file did until it was reported —
 * the page keeps its full height, the keyboard is drawn over the bottom third
 * of it, and a person typing into a field down there watches their own text
 * from behind a keyboard and has to scroll the page by hand to see it.
 *
 * The cost is the one the rest of this note spends its length avoiding: a
 * resize, and a relayout with it. It is worth paying here and nowhere else.
 * A keyboard opens when a person asks for it, once, and the reflow it causes
 * is the thing they asked for; a scroll happens continuously and the reflow it
 * used to cause was pure loss.
 *
 * **Resizing it instead was tried and it is why this note exists.** Changing
 * the size does three things, and all three were visible on a phone: the
 * engine lays the document out again; the platform reallocates the
 * `SurfaceView` behind the page, which paints one frame of black — the flash
 * the owner saw at the foot of the page — and the new geometry comes back
 * through the scroll callbacks as movement nobody made, which made the bar
 * blink. Sliding has none of those. It is a position change the compositor
 * already does every frame, the engine is never told, and the document is
 * never relaid out.
 *
 * It is also what Chrome does, and it has Chrome's consequence: while the top
 * bar is up, the last band of the viewport — the bar's own height — is behind
 * the action row and off the bottom of the screen. A page that anchors
 * something to the foot of its own window has that thing partly covered until
 * a scroll takes the bar away, at which point the whole page slides up and it
 * is entirely visible. That is the price of never resizing, and it is paid at
 * the end of a document rather than at its beginning.
 *
 * The action row is in the constant for the neighbouring reason: a page that
 * anchors a cookie banner, a chat launcher or a checkout bar to the foot of
 * its window puts controls exactly where an opaque row would be, and a control
 * drawn under a row can be read and not pressed. That height never changes, so
 * it costs nothing at all.
 *
 * Split out of the screen because it is a different subject: the rest of
 * SCR-101 is Taffy's chrome, laid out top to bottom, while this is one box with
 * a platform surface in it and a set of rules about when that surface may be
 * covered and when it may not. Those rules are the reason the file exists — see
 * [LivePageArea], which is where the one that matters is written down.
 *
 * The caller passes a `Modifier` that has already claimed the space. Nothing
 * here decides how large the page is.
 */
@Composable
internal fun BrowserPageArea(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
    startComposer: StartPageComposerSlot,
    modifier: Modifier = Modifier,
    overlayHeightPx: Int = 0,
    topOverlayHeightPx: Int = 0,
    actionRowHeightPx: Int = 0,
) {
    Box(modifier = modifier) {
        val surface = LocalPageSurface.current
        // The viewport and the slide, computed once and used by both things
        // that have to agree about them.
        //
        // The slide is what the top bar does instead of resizing anything.
        // [topOverlayHeightPx] is measured from the top of the window and so
        // already contains the status-bar and cutout strip; the surface starts
        // below that strip already, so the distance it moves is the difference
        // — and zero the moment the bar is gone.
        //
        // The bottom is a maximum of the two things that can end the page, and
        // the keyboard is deliberately one of them. The resting floor is the
        // navigation bar plus the action row, which is a sum because the row is
        // measured without the inset the column below it adds outside. The
        // other is the keyboard, and it has to clear the slide as well: the
        // surface is moved down by that much, so its own bottom edge is that
        // much lower than the padding alone would put it.
        val density = LocalDensity.current
        val edges = TaffyEdges.page
        val pageTopPx = edges.getTop(density)
        val slidePx = (topOverlayHeightPx - pageTopPx).coerceAtLeast(0)
        val keyboardPx = TaffyEdges.keyboard.getBottom(density)
        val pageBottomPx = maxOf(
            edges.getBottom(density) + actionRowHeightPx,
            keyboardPx + slidePx,
        )
        val viewport = with(density) {
            PaddingValues(top = pageTopPx.toDp(), bottom = pageBottomPx.toDp())
        }
        // Liveness is asked first, and the live host takes the slot in every
        // state. See [LivePageArea]: a real page host cannot be swapped in and
        // out per state the way a placeholder can.
        if (surface.isLive) {
            LivePageArea(
                state = state,
                surface = surface,
                onIntent = onIntent,
                startComposer = startComposer,
                overlayHeightPx = overlayHeightPx,
                topOverlayHeightPx = topOverlayHeightPx,
                viewport = viewport,
                slidePx = slidePx,
                pageTopPx = pageTopPx,
                pageBottomPx = pageBottomPx,
            )
        } else if (state.content == BrowserContent.PAGE) {
            // No engine, so there are no page pixels and the placeholder card
            // is the page's own area. Every other state is the same one the
            // live branch covers its surface with, drawn directly because there
            // is nothing underneath to cover.
            ContentArea(state = state)
        } else {
            InsteadOfThePage(
                state = state,
                onIntent = onIntent,
                startComposer = startComposer,
                overlayHeightPx = overlayHeightPx,
                topOverlayHeightPx = topOverlayHeightPx,
            )
        }
        // Last in the box, so both draw over everything else this area has —
        // including the engine's own surface, which is the only way to draw on
        // it at all. Neither takes a pointer, so every touch still reaches the
        // page underneath, which is exactly what the cut-out is asking for.
        if (state.takeover.showsFrame) {
            // Lifted clear of the chrome at **both** edges, because a frame is
            // a decoration and an edge of it behind an opaque strip is an edge
            // nobody sees. It was inset at the bottom alone while every piece
            // of chrome was down there; the address pill and the overflow are
            // at the top now, and the frame's top edge would run under them.
            TakeoverFrame(
                modifier = Modifier
                    .matchParentSize()
                    .padding(
                        top = with(LocalDensity.current) { topOverlayHeightPx.toDp() },
                        bottom = with(LocalDensity.current) { overlayHeightPx.toDp() },
                    ),
            )
        }
        // Exactly what the surface takes, and nothing else, because that sum
        // *is* the viewport. The cut-out's rectangle is a fraction of the
        // page's own viewport, so a fraction of any other rectangle points at
        // the wrong thing — off by whatever the two disagree about. That is why
        // this reads the same modifier the engine was measured with rather than
        // a set of insets that look like it.
        state.takeover.highlight
            ?.takeIf { state.takeover.showsHighlight }
            ?.let {
                TakeoverHighlight(
                    highlight = it,
                    modifier = Modifier
                        .matchParentSize()
                        .padding(viewport)
                        .offset { IntOffset(0, slidePx) },
                )
            }
        // Last of all, and over the page's own rectangle rather than the whole
        // area: the browser's chrome is what the two ways out of the hold are
        // on, so the hold may not cover it. It takes the pointer, which the
        // frame and the cut-out above deliberately do not — see
        // [TakeoverInputLock] for why a page Taffy is working stops taking
        // touches at all.
        if (state.takeover.showsLock) {
            TakeoverInputLock(
                canTakeOver = state.takeover.canTakeOver,
                modifier = Modifier
                    .matchParentSize()
                    .padding(viewport)
                    .offset { IntOffset(0, slidePx) },
            )
        }
    }
}

/**
 * The real page, and everything drawn over it.
 *
 * **The page host stays composed in every state, including while a failure
 * notice is on screen and while the tab has been nowhere at all.** Beside the
 * branch above that looks like a mistake, so: the
 * host is an `AndroidView` wrapping the `SurfaceView` the web engine renders
 * into. Leaving the composition destroys that view, and destroying that view
 * destroys the surface the compositor draws to. A screen that swapped the host
 * out whenever the state changed would therefore tear down and rebuild the
 * browser's output surface on *every navigation* — which is the one thing this
 * seam exists to avoid. So every other state is drawn **over** the page rather
 * than **instead of** it.
 *
 * The cover is opaque because what is underneath must not show through. The
 * surface sits behind the window, so every pixel Compose draws composites over
 * it; a transparent notice would be error text on top of a live web page. It is
 * also what the empty tab needed: an engine with nothing loaded still paints
 * the blank document it is on, and covering that is the only way to hide it
 * without destroying the surface that is going to draw the next page.
 *
 * **A loading page is no longer one of those states.** It used to be, and that
 * put an opaque grey rectangle over a page the reader could otherwise still
 * read — the one thing `docs/design/ux-spec.md` §2 says this area must never
 * have done to it. The cover is now for a failure and an untouched tab only,
 * both of which are cases where there genuinely is nothing worth showing
 * underneath.
 *
 * The page also draws square, and cannot do otherwise.
 * `.clip(TaffyTheme.shapes.card)` does not reach a `SurfaceView` — the window
 * resolves that surface's position, not the Compose layer — so the rounded card
 * belongs to [ContentArea]'s placeholder alone.
 */
@Composable
private fun LivePageArea(
    state: BrowserMainUiState,
    surface: PageSurface,
    onIntent: (BrowserMainIntent) -> Unit,
    startComposer: StartPageComposerSlot,
    overlayHeightPx: Int,
    topOverlayHeightPx: Int,
    viewport: PaddingValues,
    slidePx: Int,
    pageTopPx: Int,
    pageBottomPx: Int,
) {
    val slot = LocalPageSurfaceSlot.current
    val claim = remember { Any() }
    if (slot != null) {
        // The window hosts the page, so this screen reports where it belongs
        // rather than composing it. Reporting is what keeps the surface alive
        // across a navigation away and back — see [PageSurfaceSlot].
        SideEffect {
            slot.place(
                owner = claim,
                topPx = pageTopPx,
                bottomPx = pageBottomPx,
                slidePx = slidePx,
                groundArgb = state.pageBackgroundArgb,
            )
        }
        DisposableEffect(slot, claim) { onDispose { slot.park(claim) } }
    }
    Box(modifier = Modifier.fillMaxSize()) {
        val pageRect = Modifier
            .matchParentSize()
            // The one place the engine is told how tall its window is, and
            // the reason [viewport] arrives computed rather than rebuilt
            // here: the cut-out highlight has to be a fraction of the very
            // same rectangle, and two expressions that ought to agree are
            // two expressions that can stop agreeing.
            .padding(viewport)
            // And the one place it is moved without being told anything.
            // This is a placement, so the engine is never asked for a new
            // layout and the platform never reallocates the surface —
            // see this file's note on the black frame that did.
            .offset { IntOffset(0, slidePx) }
            .testTag(CONTENT_TEST_TAG)
        if (slot == null) {
            TaffyPageSurface(surface = surface, modifier = pageRect)
        } else {
            // The tagged node stays, at the page's exact size, so a semantics
            // test still finds the page's area where it always was. It paints
            // nothing: the hosted surface is behind this whole screen, and
            // anything drawn here would cover it.
            Box(modifier = pageRect)
        }
        if (state.content != BrowserContent.PAGE) {
            val coversWithBackdrop = state.content == BrowserContent.START ||
                state.content == BrowserContent.PREPARING ||
                state.content == BrowserContent.FAILED
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .then(
                        if (coversWithBackdrop) {
                            Modifier
                        } else {
                            Modifier.background(TaffyTheme.colors.surface)
                        },
                    ),
            ) {
                if (coversWithBackdrop) {
                    // The start page's own ground, drawn again as the cover.
                    // The screen root paints the same backdrop to the glass;
                    // this copy starts a status bar lower, and the wash falls
                    // over two thirds of the window, so the fraction of a
                    // percent of alpha the two disagree by at that line is
                    // below anything an eye can find. Untagged on purpose —
                    // the root's copy is the one the semantics tests name.
                    StartPageBackdrop(modifier = Modifier.matchParentSize())
                }
                InsteadOfThePage(
                    state = state,
                    onIntent = onIntent,
                    startComposer = startComposer,
                    overlayHeightPx = overlayHeightPx,
                    topOverlayHeightPx = topOverlayHeightPx,
                )
            }
        }
    }
}

/**
 * The things that stand in for a page, in one place.
 *
 * Both branches of the page area reach this, which is the point: the live
 * branch draws it inside an opaque cover over the engine's surface and the
 * placeholder branch draws it directly, and neither decides *which* of the
 * two it is. That decision is [BrowserMainUiState.content] and is made once,
 * where a host test can name the answer without a device.
 */
@Composable
private fun InsteadOfThePage(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
    startComposer: StartPageComposerSlot,
    overlayHeightPx: Int,
    topOverlayHeightPx: Int,
) {
    val density = LocalDensity.current
    val overlayHeight = with(density) { overlayHeightPx.toDp() }
    // Zero in both start states, which have no top bar at all: the start page
    // and the preparing state are drawn exactly where they were drawn before
    // the chrome was split.
    val topOverlayHeight = with(density) { topOverlayHeightPx.toDp() }
    Box(
        modifier = Modifier
            .fillMaxSize()
            .padding(top = topOverlayHeight, bottom = overlayHeight),
    ) {
        when (state.content) {
            // Never reached: the callers both ask first, because a page is drawn by
            // the surface or by the placeholder card and neither belongs here. The
            // branch is spelled out rather than folded into an `else` so that a
            // further state added to [BrowserContent] fails to compile here.
            BrowserContent.PAGE -> Unit
            // The failure is carried by the state that produced this answer, so
            // there is one to draw whenever this branch is taken.
            BrowserContent.FAILED -> state.failure?.let { failure ->
                PageFailureNotice(
                    failure = failure,
                    onReload = { onIntent(BrowserMainIntent.Reload) },
                )
            }
            BrowserContent.PREPARING -> PreparingStartArea(
                gate = state.startPageGate,
                onRetry = { onIntent(BrowserMainIntent.RetryPageTools) },
            )
            BrowserContent.START -> BrowserStartArea(
                frequent = state.frequent,
                showsFrequentSites = state.showsFrequentSites,
                composer = startComposer,
                onOpenSite = { onIntent(BrowserMainIntent.OpenSite(it)) },
            )
        }
    }
}

@Composable
private fun ContentArea(state: BrowserMainUiState) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = TaffyTheme.spacing.screenMargin)
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .heightIn(min = ContentMinimumHeight)
            .padding(TaffyTheme.spacing.screenMargin)
            .testTag(CONTENT_TEST_TAG)
            .semantics { contentDescription = state.title },
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = state.title,
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        Text(
            text = taffyString(R.string.taffy_browser_content_placeholder),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/** The tag screen SCR-101's semantics tests name for the page area. */
const val CONTENT_TEST_TAG: String = "browser_content"

/** The content area never collapses below this while a page is shown. */
private val ContentMinimumHeight = 160.dp
