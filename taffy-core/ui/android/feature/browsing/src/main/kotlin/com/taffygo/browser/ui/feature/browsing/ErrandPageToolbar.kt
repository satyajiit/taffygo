// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyAddressPill
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-110's whole chrome: the way back, the origin, and the way out.
 *
 * ## What it does not have, and why each absence is the point
 *
 * **No address bar.** The pill here is [TaffyAddressPill] with every action
 * passed as null, which is what turns it from a control into a statement. A
 * person on this screen is about to hand a credential to whoever owns the
 * origin it names, and the one question the surface has to answer is *whose
 * page is this* — a field they could type into answers a different question and
 * invites a page to make it look answered.
 *
 * **No reload and no forward.** Reloading a consent screen mid-flow is a way to
 * lose it, and forward is meaningless on a page nobody navigated backwards from
 * on purpose.
 *
 * **No action row underneath.** Decision 0119's rule that the action row never
 * hides is a rule about the browsing surface, where the row is how a person
 * moves between pages they chose. This is not that surface: there is one page,
 * one errand, and two ways out of it, and both are up here where the origin is.
 *
 * ## It does not hide
 *
 * The other half of 0119 — the top bar hides on a scroll — is refused here for
 * the reason that record gives for keeping the bar at all. `0119` refuses "a
 * top bar that is always visible" because "a permanent bar takes a band of the
 * tallest dimension away from the page **for the life of the session**". An
 * errand is not a session; it is one page a person came to do one thing on. And
 * the origin is the only defence a person has against a page that is not the
 * vendor's: a defence that scrolls away is not one.
 */
@Composable
internal fun ErrandPageToolbar(
    state: ErrandPageUiState,
    onIntent: (ErrandPageIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val chromeEdge = TaffyTheme.colors.outline
    Box(
        modifier = modifier
            .fillMaxWidth()
            .background(TaffyTheme.colors.surface)
            .drawBehind {
                val rule = TaffyBorders.standard.toPx()
                drawRect(
                    color = chromeEdge,
                    topLeft = Offset(0f, size.height - rule),
                    size = Size(size.width, rule),
                )
            }
            // The bar takes the status-bar and cutout strip, exactly as
            // `BrowserTopBar` does, so it paints its own ground all the way to
            // the glass and the page below it starts under a bar rather than
            // under the clock.
            .windowInsetsPadding(TaffyEdges.top)
            .testTag(ERRAND_TOOLBAR_TEST_TAG),
        contentAlignment = Alignment.TopCenter,
    ) {
        Row(
            modifier = Modifier
                .taffyChromeWidth()
                .padding(
                    start = TaffyTheme.spacing.screenMargin,
                    top = TaffyTheme.spacing.step,
                    end = TaffyTheme.spacing.screenMargin,
                    bottom = TaffyTheme.spacing.step,
                ),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BrowserChromeButton(
                icon = TaffyIcon.ArrowLeft,
                contentDescription = taffyString(R.string.taffy_errand_back),
                onClick = { onIntent(ErrandPageIntent.Back) },
                testTag = ERRAND_BACK_TEST_TAG,
                targetSize = ErrandToolbarTarget,
            )
            TaffyAddressPill(
                url = state.host,
                blockedCount = state.blockedCount,
                modifier = Modifier
                    .weight(1f)
                    .testTag(ERRAND_ORIGIN_TEST_TAG),
                isLoading = state.isLoading,
                isSecure = state.isSecure,
                // Every action null, deliberately. See this file's own note:
                // the pill is a statement here, not a control.
                onReload = null,
                onStopLoading = null,
                onBlockedBadge = null,
            )
            BrowserChromeButton(
                icon = TaffyIcon.X,
                contentDescription = taffyString(R.string.taffy_errand_close),
                onClick = { onIntent(ErrandPageIntent.Close) },
                testTag = ERRAND_CLOSE_TEST_TAG,
                targetSize = ErrandToolbarTarget,
            )
        }
    }
}

/** The bar itself, for the semantics test that proves what SCR-110 does not draw. */
const val ERRAND_TOOLBAR_TEST_TAG: String = "errand_toolbar"

/** The way back through the vendor's own pages. */
const val ERRAND_BACK_TEST_TAG: String = "errand_back"

/** The origin, read-only. */
const val ERRAND_ORIGIN_TEST_TAG: String = "errand_origin"

/** The way out of the errand. */
const val ERRAND_CLOSE_TEST_TAG: String = "errand_close"

/** The same 44 dp target the top bar's own control uses. */
private val ErrandToolbarTarget = 44.dp
