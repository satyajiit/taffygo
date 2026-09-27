// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Something screen SCR-101 was asked to do and did not do, said out loud.
 *
 * **Why this exists at all.** Committing a search sends nothing anywhere,
 * because TaffyGo has not chosen a search engine — see
 * [BrowserNotice.NO_SEARCH_ENGINE] and, for why that is still open,
 * `docs/open-decisions.md` OD-019. Refusing is correct. Refusing *in silence*
 * was not: the address bar closed, no page loaded, no words appeared, and an
 * honest refusal became indistinguishable from a control that does not work.
 * That is the exact failure the repository's honesty rule exists to prevent,
 * and a screen is the only place it can be fixed.
 *
 * **Why it sits beside the page rather than over it.** A
 * [PageFailureNotice] replaces the page because the page is what failed. Here
 * the page is fine and untouched — what did not happen is something the person
 * asked for a moment ago — so the notice takes a strip in the bottom chrome,
 * above the action row, and the page keeps drawing behind it. It sat directly
 * above the address bar until decision 0119 moved the address bar to the top.
 *
 * It is a live region, so it is announced when it arrives rather than waiting
 * to be found, and it stays until the person says they have read it: a notice
 * that vanishes on a timer is a notice somebody misses.
 */
@Composable
fun BrowserNoticeCard(
    notice: BrowserNotice,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .taffyChromeWidth()
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.step,
            )
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(
                width = TaffyBorders.standard,
                color = TaffyTheme.colors.outline,
                shape = TaffyTheme.shapes.card,
            )
            .padding(TaffyTheme.spacing.snug)
            .testTag(NOTICE_TEST_TAG)
            .semantics { liveRegion = LiveRegionMode.Assertive },
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = taffyString(titleOf(notice)),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.fillMaxWidth().semantics { heading() },
        )
        Text(
            text = taffyString(bodyOf(notice)),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_browser_notice_dismiss),
            onClick = onDismiss,
            testTag = NOTICE_DISMISS_TEST_TAG,
        )
    }
}

private fun titleOf(notice: BrowserNotice) = when (notice) {
    BrowserNotice.NO_SEARCH_ENGINE -> R.string.taffy_browser_no_search_title
}

private fun bodyOf(notice: BrowserNotice) = when (notice) {
    BrowserNotice.NO_SEARCH_ENGINE -> R.string.taffy_browser_no_search_body
}

/** The tags screen SCR-101's semantics tests name. */
const val NOTICE_TEST_TAG: String = "browser_notice"
const val NOTICE_DISMISS_TEST_TAG: String = "browser_notice_dismiss"
