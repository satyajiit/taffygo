// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString
import kotlinx.coroutines.delay

/**
 * The address: the runtimes people usually run, the field, and one check.
 *
 * The presets carry their version segment and that is the whole reason they
 * exist. A person who takes one starts from an address that works; a person
 * who types `…:11434` gets told, after the check, that it is not the base
 * their server takes requests on — which is a worse way to learn the same
 * thing.
 *
 * The refusal under the field is **debounced**. It is computed on every
 * keystroke, because the projection is pure and cheap, and shown only once
 * typing has stopped: an address is half-invalid for as long as it is being
 * typed, and a field that goes red at the first character is a field that
 * shouts at somebody for starting.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun EndpointAddressSection(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    val keyboard = LocalSoftwareKeyboardController.current
    val settled = rememberSettledAddress(state.address)
    Column(
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_ADDRESS_SECTION_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_presets),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            CustomEndpointAddress.Preset.entries.forEach { preset ->
                TaffySecondaryButton(
                    label = taffyString(presetLabelRes(preset)),
                    onClick = { onIntent(CustomEndpointIntent.UsePreset(preset)) },
                    enabled = !state.busy,
                    size = TaffyButtonSize.COMPACT,
                    testTag = "$CUSTOM_ENDPOINT_PRESET_TEST_TAG_PREFIX${preset.name}",
                )
            }
        }
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_address_label),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        AddressField(
            state = state,
            showRefusal = settled == state.address,
            onIntent = onIntent,
            onDone = {
                keyboard?.hide()
                onIntent(CustomEndpointIntent.Probe)
            },
        )
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_address_hint),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        val refusal = state.refusal
        if (refusal != null && settled == state.address) {
            Text(
                text = taffyString(refusalRes(refusal)),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.danger,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_REFUSAL_TEST_TAG),
            )
        } else if (state.cleartext) {
            Text(
                text = taffyString(R.string.taffy_providers_endpoint_cleartext),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.caution,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_CLEARTEXT_TEST_TAG),
            )
        }
        TaffyPrimaryButton(
            label = taffyString(
                if (state.probing) {
                    R.string.taffy_providers_endpoint_checking
                } else {
                    R.string.taffy_providers_endpoint_check
                },
            ),
            onClick = { onIntent(CustomEndpointIntent.Probe) },
            enabled = state.probeActionable,
            loading = state.probing,
            modifier = Modifier.fillMaxWidth(),
            testTag = CUSTOM_ENDPOINT_CHECK_TEST_TAG,
        )
    }
}

/** What to call this provider in the list. */
@Composable
internal fun EndpointNameSection(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    EndpointField(
        label = taffyString(R.string.taffy_providers_endpoint_name_label),
        hint = taffyString(R.string.taffy_providers_endpoint_name_hint),
        placeholder = taffyString(R.string.taffy_providers_endpoint_name_placeholder),
        value = state.name,
        enabled = !state.writing,
        keyboardType = KeyboardType.Text,
        onValueChange = { onIntent(CustomEndpointIntent.ChangeName(it)) },
        testTag = CUSTOM_ENDPOINT_NAME_TEST_TAG,
    )
}

/** The one write: address, name, models and detected runtime together. */
@Composable
internal fun EndpointSaveSection(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        if (state.saveRefused) {
            Text(
                text = taffyString(R.string.taffy_providers_endpoint_save_refused),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.danger,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_SAVE_REFUSED_TEST_TAG),
            )
        }
        TaffyPrimaryButton(
            label = taffyString(
                if (state.saving) {
                    R.string.taffy_providers_endpoint_saving
                } else {
                    R.string.taffy_providers_endpoint_save
                },
            ),
            onClick = { onIntent(CustomEndpointIntent.Save) },
            enabled = state.saveActionable,
            loading = state.saving,
            modifier = Modifier.fillMaxWidth(),
            testTag = CUSTOM_ENDPOINT_SAVE_TEST_TAG,
        )
    }
}

@Composable
private fun AddressField(
    state: CustomEndpointUiState,
    showRefusal: Boolean,
    onIntent: (CustomEndpointIntent) -> Unit,
    onDone: () -> Unit,
) {
    OutlinedTextField(
        value = state.address,
        onValueChange = { onIntent(CustomEndpointIntent.ChangeAddress(it)) },
        placeholder = { Text(taffyString(R.string.taffy_providers_endpoint_address_placeholder)) },
        isError = showRefusal && state.refusal != null,
        enabled = !state.writing,
        singleLine = true,
        keyboardOptions = KeyboardOptions(
            keyboardType = KeyboardType.Uri,
            imeAction = ImeAction.Done,
        ),
        keyboardActions = KeyboardActions(onDone = { onDone() }),
        shape = TaffyTheme.shapes.card,
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = TaffyTheme.colors.surfaceRaised,
            unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
            errorBorderColor = TaffyTheme.colors.danger,
        ),
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_ADDRESS_TEST_TAG),
    )
}

/** One labelled field with its own sentence under it. */
@Composable
private fun EndpointField(
    label: String,
    hint: String,
    placeholder: String,
    value: String,
    enabled: Boolean,
    keyboardType: KeyboardType,
    onValueChange: (String) -> Unit,
    testTag: String,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
    ) {
        Text(
            text = label,
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        OutlinedTextField(
            value = value,
            onValueChange = onValueChange,
            placeholder = { Text(placeholder) },
            enabled = enabled,
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = keyboardType),
            shape = TaffyTheme.shapes.card,
            colors = OutlinedTextFieldDefaults.colors(
                focusedContainerColor = TaffyTheme.colors.surfaceRaised,
                unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
            ),
            modifier = Modifier.fillMaxWidth().testTag(testTag),
        )
        Text(
            text = hint,
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/** The address as it stood once typing stopped. */
@Composable
private fun rememberSettledAddress(address: String): String {
    var settled by remember { mutableStateOf(address) }
    LaunchedEffect(address) {
        delay(ADDRESS_SETTLE_MILLIS)
        settled = address
    }
    return settled
}

private fun presetLabelRes(preset: CustomEndpointAddress.Preset): Int = when (preset) {
    CustomEndpointAddress.Preset.OLLAMA -> R.string.taffy_providers_endpoint_preset_ollama
    CustomEndpointAddress.Preset.LM_STUDIO -> R.string.taffy_providers_endpoint_preset_lm_studio
    CustomEndpointAddress.Preset.LLAMA_CPP -> R.string.taffy_providers_endpoint_preset_llama_cpp
    CustomEndpointAddress.Preset.VLLM -> R.string.taffy_providers_endpoint_preset_vllm
}

private fun refusalRes(refusal: CustomEndpointAddress.Refusal): Int = when (refusal) {
    CustomEndpointAddress.Refusal.TOO_LONG -> R.string.taffy_providers_endpoint_refusal_long
    CustomEndpointAddress.Refusal.NOT_AN_ADDRESS ->
        R.string.taffy_providers_endpoint_refusal_not_an_address

    CustomEndpointAddress.Refusal.CLEARTEXT_NOT_LOCAL ->
        R.string.taffy_providers_endpoint_refusal_cleartext

    CustomEndpointAddress.Refusal.CARRIES_CREDENTIALS ->
        R.string.taffy_providers_endpoint_refusal_credentials

    CustomEndpointAddress.Refusal.CARRIES_QUERY -> R.string.taffy_providers_endpoint_refusal_query
    CustomEndpointAddress.Refusal.CARRIES_FRAGMENT ->
        R.string.taffy_providers_endpoint_refusal_fragment
}

/** The tags screen SCR-418's semantics tests name. */
const val CUSTOM_ENDPOINT_ADDRESS_SECTION_TEST_TAG: String = "providers_endpoint_address_section"
const val CUSTOM_ENDPOINT_ADDRESS_TEST_TAG: String = "providers_endpoint_address"
const val CUSTOM_ENDPOINT_PRESET_TEST_TAG_PREFIX: String = "providers_endpoint_preset_"
const val CUSTOM_ENDPOINT_REFUSAL_TEST_TAG: String = "providers_endpoint_refusal"
const val CUSTOM_ENDPOINT_CLEARTEXT_TEST_TAG: String = "providers_endpoint_cleartext"
const val CUSTOM_ENDPOINT_CHECK_TEST_TAG: String = "providers_endpoint_check"
const val CUSTOM_ENDPOINT_NAME_TEST_TAG: String = "providers_endpoint_name"
const val CUSTOM_ENDPOINT_SAVE_TEST_TAG: String = "providers_endpoint_save"
const val CUSTOM_ENDPOINT_SAVE_REFUSED_TEST_TAG: String = "providers_endpoint_save_refused"

/** How long typing has to stop before the address is judged out loud. */
private const val ADDRESS_SETTLE_MILLIS = 600L
