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
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.ui.TaffyBrandMark
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-108 — an honest error page, in TaffyGo's own voice.
 *
 * Each failure has its own words, its own mark, and a retry only where
 * retrying could plausibly work. A certificate that is not valid does not
 * become valid because someone pressed a button, so that page offers
 * understanding instead. The engine's own error document stays underneath;
 * this is drawn over it so the person never sees Chromium's unbranded page
 * for a failure TaffyGo has words for.
 *
 * It is a page, not a card. The start page's weather is already the ground,
 * and a raised slab on that weather read as a dialog sitting on a page that
 * had failed — the same construction the preparing state already refused.
 * The brand mark is the page's, the reason glyph sits in a sunken disc
 * (never amber: amber means Taffy is working), and the title takes the
 * display role every other empty page uses.
 */
@Composable
fun PageFailureNotice(
    failure: PageLoadFailure,
    onReload: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(TaffyTheme.spacing.screenMargin)
            .testTag(FAILURE_TEST_TAG)
            .semantics { liveRegion = LiveRegionMode.Assertive },
        verticalArrangement = Arrangement.spacedBy(
            TaffyTheme.spacing.snug,
            Alignment.CenterVertically,
        ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        TaffyBrandMark(size = MarkSize, contentDescription = null)
        Box(
            modifier = Modifier
                .size(ReasonSize)
                .clip(CircleShape)
                .background(TaffyTheme.colors.surfaceSunken),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = iconOf(failure),
                contentDescription = null,
                modifier = Modifier.size(GlyphSize),
                tint = tintOf(failure, TaffyTheme.colors.danger, TaffyTheme.colors.textPrimary),
            )
        }
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = taffyString(titleOf(failure)),
                style = TaffyTheme.typography.display,
                color = TaffyTheme.colors.textPrimary,
                textAlign = TextAlign.Center,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                text = taffyString(bodyOf(failure)),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textSecondary,
                textAlign = TextAlign.Center,
            )
        }
        if (failure.retryable) {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_error_retry),
                onClick = onReload,
                icon = TaffyIcon.ArrowClockwise,
                testTag = RETRY_TEST_TAG,
            )
        }
    }
}

private fun titleOf(failure: PageLoadFailure) = when (failure) {
    PageLoadFailure.OFFLINE -> R.string.taffy_error_offline_title
    PageLoadFailure.NAME_NOT_RESOLVED -> R.string.taffy_error_name_title
    PageLoadFailure.UNREACHABLE -> R.string.taffy_error_unreachable_title
    PageLoadFailure.TIMED_OUT -> R.string.taffy_error_timeout_title
    PageLoadFailure.CERTIFICATE_INVALID -> R.string.taffy_error_certificate_title
    PageLoadFailure.PAGE_CRASHED -> R.string.taffy_error_crashed_title
}

private fun bodyOf(failure: PageLoadFailure) = when (failure) {
    PageLoadFailure.OFFLINE -> R.string.taffy_error_offline_body
    PageLoadFailure.NAME_NOT_RESOLVED -> R.string.taffy_error_name_body
    PageLoadFailure.UNREACHABLE -> R.string.taffy_error_unreachable_body
    PageLoadFailure.TIMED_OUT -> R.string.taffy_error_timeout_body
    PageLoadFailure.CERTIFICATE_INVALID -> R.string.taffy_error_certificate_body
    PageLoadFailure.PAGE_CRASHED -> R.string.taffy_error_crashed_body
}

private fun iconOf(failure: PageLoadFailure): ImageVector = when (failure) {
    PageLoadFailure.OFFLINE -> TaffyIcon.Cloud
    PageLoadFailure.NAME_NOT_RESOLVED -> TaffyIcon.GlobeSimple
    PageLoadFailure.UNREACHABLE -> TaffyIcon.Prohibit
    PageLoadFailure.TIMED_OUT -> TaffyIcon.ClockCounterClockwise
    PageLoadFailure.CERTIFICATE_INVALID -> TaffyIcon.LockSimple
    PageLoadFailure.PAGE_CRASHED -> TaffyIcon.Warning
}

private fun tintOf(failure: PageLoadFailure, danger: Color, ink: Color): Color =
    if (failure == PageLoadFailure.CERTIFICATE_INVALID) danger else ink

/** The tags screen SCR-108's semantics tests name. */
const val FAILURE_TEST_TAG: String = "page_failure"
const val RETRY_TEST_TAG: String = "page_failure_retry"

private val MarkSize = 64.dp
private val ReasonSize = 48.dp
private val GlyphSize = 22.dp
