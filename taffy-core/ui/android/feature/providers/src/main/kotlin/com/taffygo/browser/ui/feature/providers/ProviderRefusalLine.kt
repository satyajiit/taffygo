// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import android.text.format.DateUtils
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.RosterProviderRefusal
import com.taffygo.browser.ui.core.model.RosterRefusalState
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The vendor's last refusal, said where the credential is shown.
 *
 * Screens SCR-415 and SCR-419 both draw it, under the model line and under
 * the row, and it reads the same in both places on purpose: a person who
 * sees "rate limiting" on the list and then opens the page must not find a
 * different diagnosis there. One sentence per reason, in the caution tone
 * rather than the danger tone, because every reason here is a working
 * credential the vendor is declining to spend (registry document section
 * 5.4) — and the sentence says so, so nobody replaces a key that was never
 * the problem.
 *
 * The "when" line is drawn only where the profile stamped the refusal; the
 * core's own reading is monotonic and cannot be shown as a time.
 */
@Composable
internal fun ProviderRefusalLine(
    refusal: RosterRefusalState,
    displayName: String,
    modifier: Modifier = Modifier,
    testTag: String? = null,
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
    ) {
        Text(
            text = refusalSentence(refusal.refusal, displayName),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.caution,
            modifier = if (testTag == null) Modifier else Modifier.testTag(testTag),
        )
        refusal.observedAtEpochMillis?.let { seenAt ->
            Text(
                text = taffyString(
                    R.string.taffy_providers_refusal_when,
                    DateUtils.getRelativeTimeSpanString(seenAt).toString(),
                ),
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

/** One sentence per reason, and no fallback: a new reason is a new sentence. */
@Composable
private fun refusalSentence(refusal: RosterProviderRefusal, displayName: String): String =
    when (refusal) {
        RosterProviderRefusal.RATE_LIMIT ->
            taffyString(R.string.taffy_providers_refusal_rate_limit, displayName)

        RosterProviderRefusal.BILLING -> taffyString(R.string.taffy_providers_refusal_billing)
        RosterProviderRefusal.OVERLOADED ->
            taffyString(R.string.taffy_providers_refusal_overloaded, displayName)
    }
