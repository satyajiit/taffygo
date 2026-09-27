// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The redirect wait with its way back (decision 0095 section 2).
 *
 * Above the rule it is the ordinary waiting panel: the tab is open on the
 * vendor's page and this screen keeps up by itself. Below it is what the
 * screen it replaces never had — a place to paste what the vendor showed, or
 * the address its page landed on, when the redirect does not come back. The
 * value is handed to the browser whole and never inspected here; the browser
 * decides whether it belongs to the running flow, and a no is drawn as a
 * caution rather than as an error, because the person can try again until
 * the flow's deadline.
 */
@Composable
internal fun WaitingWithCodeEntryPanel(
    entry: ManualCodeEntry,
    onIntent: (ProviderSignInIntent) -> Unit,
) {
    val keyboard = LocalSoftwareKeyboardController.current
    val submit = {
        keyboard?.hide()
        onIntent(ProviderSignInIntent.SubmitManualCode)
    }
    SignInPanel(testTag = PROVIDER_SIGN_IN_WAITING_TEST_TAG) {
        Text(
            text = taffyString(R.string.taffy_providers_signin_waiting_title),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
        )
        Text(
            text = taffyString(R.string.taffy_providers_signin_waiting_body),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        Text(
            text = taffyString(R.string.taffy_providers_signin_code_entry_title),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.testTag(PROVIDER_SIGN_IN_CODE_ENTRY_TEST_TAG),
        )
        Text(
            text = taffyString(R.string.taffy_providers_signin_code_entry_body),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        ManualCodeField(entry = entry, onIntent = onIntent, onDone = submit)
        TaffyPrimaryButton(
            label = taffyString(
                if (entry.submitting) {
                    R.string.taffy_providers_signin_code_entry_submitting
                } else {
                    R.string.taffy_providers_signin_code_entry_submit
                },
            ),
            onClick = submit,
            enabled = entry.submittable,
            loading = entry.submitting,
            icon = TaffyIcon.Check,
            size = TaffyButtonSize.COMPACT,
            modifier = Modifier.fillMaxWidth(),
            testTag = PROVIDER_SIGN_IN_CODE_ENTRY_SUBMIT_TEST_TAG,
        )
        if (entry.rejected) {
            Text(
                text = taffyString(R.string.taffy_providers_signin_code_entry_rejected),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.caution,
                modifier = Modifier
                    .testTag(PROVIDER_SIGN_IN_CODE_ENTRY_REJECTED_TEST_TAG)
                    .semantics { liveRegion = LiveRegionMode.Polite },
            )
        }
        SignInCancelAction(onIntent = onIntent)
    }
}

/**
 * One line, shown as typed.
 *
 * Visible rather than masked: a code the vendor displayed on a page is not a
 * secret in the way a key is, and the likeliest failure is a partial paste
 * that only a person who can read the field will notice. No autocorrect and
 * no capitalisation, because the value is the vendor's and any change to it
 * is a code that does not work.
 */
@Composable
private fun ManualCodeField(
    entry: ManualCodeEntry,
    onIntent: (ProviderSignInIntent) -> Unit,
    onDone: () -> Unit,
) {
    OutlinedTextField(
        value = entry.draft,
        onValueChange = { onIntent(ProviderSignInIntent.ManualCodeChanged(it)) },
        label = { Text(taffyString(R.string.taffy_providers_signin_code_entry_label)) },
        isError = entry.rejected,
        enabled = !entry.submitting,
        singleLine = true,
        keyboardOptions = KeyboardOptions(
            keyboardType = KeyboardType.Uri,
            autoCorrectEnabled = false,
            imeAction = ImeAction.Done,
        ),
        keyboardActions = KeyboardActions(onDone = { onDone() }),
        shape = TaffyTheme.shapes.card,
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = TaffyTheme.colors.surfaceRaised,
            unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
            focusedBorderColor = TaffyTheme.colors.outline,
            unfocusedBorderColor = TaffyTheme.colors.outline,
            errorBorderColor = TaffyTheme.colors.caution,
        ),
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_SIGN_IN_CODE_ENTRY_FIELD_TEST_TAG),
    )
}

/** The tags screen SCR-416's manual-code fallback carries. */
const val PROVIDER_SIGN_IN_CODE_ENTRY_TEST_TAG: String = "providers_sign_in_code_entry"
const val PROVIDER_SIGN_IN_CODE_ENTRY_FIELD_TEST_TAG: String = "providers_sign_in_code_entry_field"
const val PROVIDER_SIGN_IN_CODE_ENTRY_SUBMIT_TEST_TAG: String =
    "providers_sign_in_code_entry_submit"
const val PROVIDER_SIGN_IN_CODE_ENTRY_REJECTED_TEST_TAG: String =
    "providers_sign_in_code_entry_rejected"
