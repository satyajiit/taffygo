// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.ui.TaffyBrandLockup
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * What an empty tab draws instead of the start page, until page intelligence
 * is installed.
 *
 * Shared by screen SCR-101 and screen SCR-102 so the two surfaces cannot
 * disagree about the same wait. The start page itself is not shown: recents,
 * the private entry and the address box are the product looking ready, and
 * it is not ready until the install has finished.
 */
@Composable
internal fun PreparingStartArea(
    gate: StartPageGate,
    onRetry: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val body = preparingBody(gate)
    val progress = gate.fraction
    val progressDescription = progress?.let {
        taffyString(R.string.taffy_start_preparing_progress, (it * 100f).toInt())
    }
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(TaffyTheme.spacing.screenMargin)
            .testTag(PREPARING_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(
            TaffyTheme.spacing.snug,
            Alignment.CenterVertically,
        ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        TaffyBrandLockup(height = LockupHeight)
        Text(
            text = taffyString(R.string.taffy_start_preparing_title),
            style = TaffyTheme.typography.display,
            color = TaffyTheme.colors.textPrimary,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .fillMaxWidth()
                .semantics { heading() },
        )
        Text(
            text = taffyString(body),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth(),
        )
        if (gate.hold?.canRetry != false) {
            if (progress != null) {
                LinearProgressIndicator(
                    progress = { progress },
                    modifier = Modifier
                        .fillMaxWidth()
                        .testTag(PREPARING_PROGRESS_TEST_TAG)
                        .semantics {
                            if (progressDescription != null) {
                                contentDescription = progressDescription
                            }
                        },
                    color = TaffyTheme.colors.textPrimary,
                    trackColor = TaffyTheme.colors.outline,
                )
            } else {
                LinearProgressIndicator(
                    modifier = Modifier
                        .fillMaxWidth()
                        .testTag(PREPARING_PROGRESS_TEST_TAG),
                    color = TaffyTheme.colors.textPrimary,
                    trackColor = TaffyTheme.colors.outline,
                )
            }
        }
        if (gate.canRetry) {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_preparing_parts_retry),
                onClick = onRetry,
                testTag = PREPARING_RETRY_TEST_TAG,
            )
        }
    }
}

/**
 * The one sentence under the heading.
 *
 * A browser that has described nothing is answered first and by name. Every
 * other line here is a fact about a download, and there is no download to
 * have a fact about until the delivery plane has spoken — so this used to
 * fall through to "installing" and describe an install that had never been
 * planned, under a bar that never moved.
 */
private fun preparingBody(gate: StartPageGate): Int = if (!gate.answered) {
    R.string.taffy_start_preparing_not_started
} else when (gate.hold) {
    TaffyPartHold.CONNECTION_NOT_ALLOWED -> R.string.taffy_start_preparing_offline
    TaffyPartHold.NOT_AVAILABLE_FOR_THIS_DEVICE,
    TaffyPartHold.NOT_PUBLISHED,
    -> R.string.taffy_start_preparing_unavailable
    TaffyPartHold.PRODUCT_DEFECT,
    TaffyPartHold.TURNED_OFF,
    -> R.string.taffy_preparing_parts_product_defect
    TaffyPartHold.ATTEMPTS_SPENT,
    TaffyPartHold.WRONG_CONTENTS,
    -> R.string.taffy_start_preparing_failed
    null -> when (gate.availability) {
        TaffyPartAvailability.CHECKING -> R.string.taffy_start_preparing_installing
        else -> R.string.taffy_start_preparing_body
    }
}

/** The tags the preparing state's semantics tests name. */
const val PREPARING_TEST_TAG: String = "start_preparing"
const val PREPARING_PROGRESS_TEST_TAG: String = "start_preparing_progress"
const val PREPARING_RETRY_TEST_TAG: String = "start_preparing_retry"

private val LockupHeight = 40.dp
