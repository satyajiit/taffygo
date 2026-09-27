// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The deliberate second tap before something on this phone is removed.
 *
 * **One sheet, three screens.** Screen SCR-415 signs out of a provider, screen
 * SCR-419 signs out of one from the list of what is connected, and screen
 * SCR-418 removes an endpoint a person defined. All three are the same
 * gesture — an unrecoverable local deletion — and the reason they share this
 * composable rather than each drawing their own is that a second copy is how
 * two screens end up warning about the same act in different words, with the
 * weaker wording winning wherever a person happened to arrive from.
 *
 * What is *not* shared is the write. Each screen keeps its own intent and its
 * own call, because they remove different things; the one thing they must
 * never disagree about is what they told the person first.
 *
 * A bottom sheet rather than an inline confirmation, because the thing being
 * confirmed is the removal of the only copy of a record this phone holds.
 */
@Composable
internal fun ProviderRemovalSheet(
    title: String,
    body: String,
    confirmLabel: String,
    running: Boolean,
    onConfirm: () -> Unit,
    onCancel: () -> Unit,
    testTag: String,
    confirmTestTag: String,
) {
    TaffyBottomSheet(
        title = title,
        onDismissRequest = onCancel,
        testTag = testTag,
    ) {
        Text(
            text = body,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
        TaffyDangerButton(
            label = confirmLabel,
            onClick = onConfirm,
            enabled = !running,
            loading = running,
            modifier = Modifier.fillMaxWidth(),
            testTag = confirmTestTag,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_providers_signout_cancel),
            onClick = onCancel,
            enabled = !running,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}
