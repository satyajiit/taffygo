// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Constraints
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.LocalPageSurface
import com.taffygo.browser.ui.core.ui.LocalPageSurfaceSlot
import com.taffygo.browser.ui.core.ui.PageSurfaceSlot
import com.taffygo.browser.ui.core.ui.ReleasePoppedDestinationState
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyDestinationGroups
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPageSurface
import com.taffygo.browser.ui.core.ui.presentation

/**
 * The navigation host: the one place a destination becomes a screen.
 *
 * This is the only file in the UI layer that names more than one feature, and it
 * is the application shell, which is the module allowed to. Compact widths
 * draw one screen. Medium and expanded widths keep a settings, you, library
 * or workspace list beside its detail, which is how a tablet is not a
 * stretched phone.
 */
@Composable
fun TaffyNavHost(
    backStack: BackStack,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    BackHandler(enabled = backStack.canGoBack) { navigator.goBack() }

    // A screen's saved state belongs to the destination showing it, so it ends
    // where the destination does. This is the only place that knows both — the
    // stack as it is now and the stack as it was — which is why the release
    // lives beside the drawing rather than inside a screen.
    ReleasePoppedDestinationState(backStack)

    val twoPane = TaffyTheme.windowWidth.showsTwoPane
    // No inset padding here, deliberately. This box is the background of every
    // screen, and a background that stops short of the glass is the thing
    // edge-to-edge exists to remove. Each surface below pulls its own words and
    // controls back off the bars, which is the only part that has to move.
    val pageSlot = remember { PageSurfaceSlot() }
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(TaffyTheme.colors.surface),
    ) {
        // First child, so every destination draws over it. The page is
        // composed once here for the life of the window and is placed rather
        // than mounted — see [PageSurfaceSlot] for why that is the fix for the
        // black frame on back navigation.
        HostedPageLayer(slot = pageSlot)
        CompositionLocalProvider(LocalPageSurfaceSlot provides pageSlot) {
            AnimatedContent(
                targetState = backStack,
                modifier = Modifier.fillMaxSize(),
                transitionSpec = {
                    // The browsing surface arrives and leaves without moving. See
                    // [movesTheBrowsingSurface] for why that is a correctness rule
                    // here rather than a matter of taste.
                    if (movesTheBrowsingSurface(initialState, targetState)) {
                        return@AnimatedContent EnterTransition.None togetherWith ExitTransition.None
                    }
                    val enteringOffset: (Int) -> Int
                    val leavingOffset: (Int) -> Int
                    if (isBackNavigation(initialState, targetState)) {
                        enteringOffset = { width -> -width / NavigationTravelDivisor }
                        leavingOffset = { width -> width / NavigationTravelDivisor }
                    } else {
                        enteringOffset = { width -> width / NavigationTravelDivisor }
                        leavingOffset = { width -> -width / NavigationTravelDivisor }
                    }
                    (
                        slideInHorizontally(
                            animationSpec = tween(
                                NavigationDurationMs,
                                easing = FastOutSlowInEasing,
                            ),
                            initialOffsetX = enteringOffset,
                        ) +
                            fadeIn(animationSpec = tween(NavigationFadeInMs))
                    ).togetherWith(
                        slideOutHorizontally(
                            animationSpec = tween(
                                NavigationDurationMs,
                                easing = FastOutSlowInEasing,
                            ),
                            targetOffsetX = leavingOffset,
                        ) +
                            fadeOut(animationSpec = tween(NavigationFadeOutMs)),
                    )
                },
                // The frame is keyed on the screen, never on an overlay over it:
                // the Ask overlay rising over the page is the same content, so
                // nothing is torn down and the page beneath keeps rendering.
                contentKey = { stack -> stack.presentation().base.route },
            ) { visibleStack ->
                val presentation = visibleStack.presentation()
                val destination = presentation.base
                val overlay = presentation.overlay
                when {
                    twoPane && TaffyDestinationGroups.isSettings(destination) ->
                        OverOverlay(overlay, navigator) {
                            SettingsTwoPane(destination = destination, navigator = navigator)
                        }
                    twoPane && TaffyDestinationGroups.isYou(destination) ->
                        OverOverlay(overlay, navigator) {
                            YouTwoPane(destination = destination, navigator = navigator)
                        }
                    twoPane && TaffyDestinationGroups.isLibrary(destination) ->
                        OverOverlay(overlay, navigator) {
                            LibraryTwoPane(destination = destination, navigator = navigator)
                        }
                    twoPane && TaffyDestinationGroups.isWorkspace(destination) ->
                        OverOverlay(overlay, navigator) {
                            WorkspaceTwoPane(destination = destination, navigator = navigator)
                        }
                    else -> SinglePaneHost(
                        destination = destination,
                        navigator = navigator,
                        overlay = overlay,
                    )
                }
            }
        }
    }
}

/**
 * The one page surface, composed for the life of the window.
 *
 * It is never in a conditional branch and never leaves the composition, which
 * is the whole of the fix: an `AndroidView` that leaves the composition has its
 * `View` removed from the window, and a `SurfaceView` removed from the window
 * has its surface destroyed. What moves is the placement, read inside the
 * measure lambda so a screen's report and the placement it causes land in the
 * same frame.
 *
 * A parked page is placed off the bottom of the window at its own size. It is
 * never made `GONE` — that is a surface teardown, which is the thing this
 * exists to stop.
 */
@Composable
private fun HostedPageLayer(slot: PageSurfaceSlot) {
    val surface = LocalPageSurface.current
    if (!surface.isLive) return
    val ground = slot.placement.takeIf { it.onScreen }?.groundArgb?.let(::Color)
    Box(
        modifier = Modifier
            .fillMaxSize()
            .then(if (ground != null) Modifier.background(ground) else Modifier),
    ) {
        TaffyPageSurface(
            surface = surface,
            modifier = Modifier.layout { measurable, constraints ->
                // A deferred read: the placement is looked at here rather than
                // in composition, so a screen reporting a new one costs a
                // measure pass and not a recomposition of this layer.
                val placement = slot.placement
                val width = constraints.maxWidth
                val height = (constraints.maxHeight - placement.topPx - placement.bottomPx)
                    .coerceAtLeast(0)
                val placeable = measurable.measure(Constraints.fixed(width, height))
                layout(constraints.maxWidth, constraints.maxHeight) {
                    // Parked is the same size, off the bottom edge. Same
                    // surface, same compositor, no teardown.
                    val y = if (placement.onScreen) {
                        placement.topPx + placement.slidePx
                    } else {
                        constraints.maxHeight
                    }
                    placeable.place(0, y)
                }
            },
        )
    }
}

/** A two-pane host with the current overlay, if any, composed over the whole window. */
@Composable
private fun OverOverlay(
    overlay: TaffyDestination?,
    navigator: TaffyNavigator,
    content: @Composable () -> Unit,
) {
    Box(modifier = Modifier.fillMaxSize()) {
        content()
        if (overlay != null) {
            OverlayHost(overlay = overlay, navigator = navigator)
        }
    }
}

/**
 * Whether either side of this transition is the browsing surface.
 *
 * An overlay over the surface counts as the surface: the page is still there
 * beneath it, so the same rule applies for the same reason.
 *
 * That surface is exempt from the shared-axis transition, and not as a style
 * choice. Screen SCR-101 hosts the web page in a `SurfaceView`, and a
 * `SurfaceView` is composited by the window rather than painted by the view
 * above it. `slideInHorizontally` and `fadeIn` work by putting a
 * `graphicsLayer` translation and an alpha on an ancestor — neither of which
 * reaches that surface. Animate this transition and the chrome slides while the
 * page sits motionless behind it at full opacity, which looks broken in a way
 * no one would guess from reading this file.
 *
 * It is also the right answer for the product. A browser's primary surface is
 * where the user already is; it does not travel in from the edge.
 */
internal fun movesTheBrowsingSurface(initial: BackStack, target: BackStack): Boolean =
    carriesThePage(initial.presentation().base) || carriesThePage(target.presentation().base)

/**
 * Whether this destination puts the engine's own surface on screen.
 *
 * Two do. SCR-101 is the browsing surface, and SCR-110 is the page an errand
 * runs on — a different screen with a different chrome, drawing the same
 * `SurfaceView` through the same host. The rule above is about that view and
 * not about either screen, so both sides of it have to be named or the errand
 * page animates its chrome past a page that cannot move with it.
 */
private fun carriesThePage(destination: TaffyDestination): Boolean =
    destination is TaffyDestination.BrowserMain || destination is TaffyDestination.ErrandPage

/** A pop returns to a prefix; replacement navigations still move forward. */
internal fun isBackNavigation(initial: BackStack, target: BackStack): Boolean =
    target.entries.size < initial.entries.size &&
        initial.entries.take(target.entries.size) == target.entries

// Screen-catalog motion is a quick shared-axis transition capped at 200 ms.
private const val NavigationDurationMs = 200
private const val NavigationFadeInMs = 150
private const val NavigationFadeOutMs = 100
private const val NavigationTravelDivisor = 5
