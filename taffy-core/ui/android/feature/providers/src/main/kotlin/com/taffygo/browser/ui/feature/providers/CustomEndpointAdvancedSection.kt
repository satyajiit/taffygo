// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The advanced part of the page: the key, for the servers that ask for one.
 *
 * Advanced and optional, because most model servers people run themselves ask
 * for nothing and the keyless path is the one this screen is mostly walked
 * down. It sits under the check rather than in the main run of fields for the
 * same reason, and the field's own Done action spends the check — a person who
 * has just typed a key wants the same question asked again with it, not a
 * scroll back up to the button.
 *
 * Closed until somebody opens it, with one exception the view model owns: a
 * check that came back saying the server will list nothing without a
 * credential opens it, so the field being asked for is on screen beside the
 * sentence asking for it.
 */
@Composable
internal fun EndpointAdvancedSection(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    val keyboard = LocalSoftwareKeyboardController.current
    Column(
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_ADVANCED_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = taffyString(R.string.taffy_providers_endpoint_advanced),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.weight(1f),
            )
            TaffySecondaryButton(
                label = taffyString(
                    if (state.advancedOpen) {
                        R.string.taffy_providers_key_hide
                    } else {
                        R.string.taffy_providers_key_show
                    },
                ),
                onClick = { onIntent(CustomEndpointIntent.ToggleAdvanced) },
                size = TaffyButtonSize.COMPACT,
                testTag = CUSTOM_ENDPOINT_ADVANCED_TOGGLE_TEST_TAG,
            )
        }
        if (!state.advancedOpen) return@Column
        Text(
            text = taffyString(R.string.taffy_providers_key_label),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        EndpointKeyField(
            state = state,
            onIntent = onIntent,
            onDone = {
                keyboard?.hide()
                onIntent(CustomEndpointIntent.Probe)
            },
        )
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_key_hint),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        // Said only where it is true, and it is the whole meaning of an empty
        // field on an edit: nothing can show a stored key again, so a page that
        // left this out would be asking somebody to retype one to change a port.
        if (state.credentialHeld) {
            Text(
                text = taffyString(R.string.taffy_providers_endpoint_key_stored),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_KEY_STORED_TEST_TAG),
            )
        }
        if (state.keyStoreRefused) {
            Text(
                text = taffyString(R.string.taffy_providers_key_problem_store),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.danger,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_KEY_PROBLEM_TEST_TAG),
            )
        }
        Text(
            text = taffyString(R.string.taffy_providers_key_security),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/**
 * The key field: masked, revealable, and never prefilled.
 *
 * The same shape as screen SCR-415's key form — dots until a person asks to
 * see the characters, and a word rather than a glyph for the toggle, because
 * the palette carries only the struck-through eye and "Show"/"Hide" needs no
 * legend. What it does not have is that form's verdict border: the catalog can
 * say what a vendor's keys begin with, and nobody can say that about a key for
 * a server somebody runs themselves, so there is nothing to judge before the
 * check.
 */
@Composable
private fun EndpointKeyField(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
    onDone: () -> Unit,
) {
    OutlinedTextField(
        value = state.key,
        onValueChange = { onIntent(CustomEndpointIntent.ChangeKey(it)) },
        placeholder = { Text(taffyString(R.string.taffy_providers_key_placeholder)) },
        isError = state.keyStoreRefused,
        enabled = !state.writing,
        singleLine = true,
        visualTransformation = if (state.keyRevealed) {
            VisualTransformation.None
        } else {
            PasswordVisualTransformation()
        },
        keyboardOptions = KeyboardOptions(
            keyboardType = KeyboardType.Password,
            imeAction = ImeAction.Done,
        ),
        keyboardActions = KeyboardActions(onDone = { onDone() }),
        shape = TaffyTheme.shapes.card,
        trailingIcon = {
            TaffySecondaryButton(
                label = taffyString(
                    if (state.keyRevealed) {
                        R.string.taffy_providers_key_hide
                    } else {
                        R.string.taffy_providers_key_show
                    },
                ),
                onClick = { onIntent(CustomEndpointIntent.ToggleKeyVisible) },
                size = TaffyButtonSize.COMPACT,
                modifier = Modifier.padding(end = TaffyTheme.spacing.step),
                testTag = CUSTOM_ENDPOINT_KEY_REVEAL_TEST_TAG,
            )
        },
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = TaffyTheme.colors.surfaceRaised,
            unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
            errorBorderColor = TaffyTheme.colors.danger,
        ),
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_KEY_TEST_TAG),
    )
}

/** The tags the advanced part names in screen SCR-418's semantics tests. */
const val CUSTOM_ENDPOINT_ADVANCED_TEST_TAG: String = "providers_endpoint_advanced"
const val CUSTOM_ENDPOINT_ADVANCED_TOGGLE_TEST_TAG: String = "providers_endpoint_advanced_toggle"
const val CUSTOM_ENDPOINT_KEY_TEST_TAG: String = "providers_endpoint_key"
const val CUSTOM_ENDPOINT_KEY_REVEAL_TEST_TAG: String = "providers_endpoint_key_reveal"
const val CUSTOM_ENDPOINT_KEY_STORED_TEST_TAG: String = "providers_endpoint_key_stored"
const val CUSTOM_ENDPOINT_KEY_PROBLEM_TEST_TAG: String = "providers_endpoint_key_problem"
