// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.KeyboardType
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Add or edit one person. No national-id field. */
@Composable
internal fun SavedDetailsEditorSheet(
    state: SavedDetailsUiState,
    onIntent: (SavedDetailsIntent) -> Unit,
) {
    val editor = state.editor ?: return
    val adding = editor.id == null
    TaffyBottomSheet(
        title = taffyString(
            if (adding) {
                R.string.taffy_details_editor_add_title
            } else {
                R.string.taffy_details_editor_edit_title
            },
        ),
        onDismissRequest = { onIntent(SavedDetailsIntent.DismissEditor) },
        testTag = DETAILS_EDITOR_TEST_TAG,
    ) {
        if (state.confirmDelete) {
            Text(
                text = taffyString(R.string.taffy_details_delete_title),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = taffyString(R.string.taffy_details_delete_body),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textSecondary,
            )
            TaffyDangerButton(
                label = taffyString(R.string.taffy_details_delete_confirm),
                onClick = { onIntent(SavedDetailsIntent.ConfirmDelete) },
                modifier = Modifier.fillMaxWidth(),
                testTag = DETAILS_DELETE_CONFIRM_TEST_TAG,
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_details_delete_keep),
                onClick = { onIntent(SavedDetailsIntent.CancelDelete) },
                modifier = Modifier.fillMaxWidth(),
            )
            return@TaffyBottomSheet
        }
        DetailsField(
            value = editor.givenName,
            label = taffyString(R.string.taffy_details_given_name),
            testTag = DETAILS_GIVEN_TEST_TAG,
            onChange = { onIntent(SavedDetailsIntent.EditorChanged(editor.copy(givenName = it))) },
        )
        DetailsField(
            value = editor.familyName,
            label = taffyString(R.string.taffy_details_family_name),
            testTag = DETAILS_FAMILY_TEST_TAG,
            onChange = { onIntent(SavedDetailsIntent.EditorChanged(editor.copy(familyName = it))) },
        )
        DetailsField(
            value = editor.email,
            label = taffyString(R.string.taffy_details_email),
            testTag = DETAILS_EMAIL_TEST_TAG,
            keyboard = KeyboardType.Email,
            onChange = { onIntent(SavedDetailsIntent.EditorChanged(editor.copy(email = it))) },
        )
        DetailsField(
            value = editor.phone,
            label = taffyString(R.string.taffy_details_phone),
            testTag = DETAILS_PHONE_TEST_TAG,
            keyboard = KeyboardType.Phone,
            onChange = { onIntent(SavedDetailsIntent.EditorChanged(editor.copy(phone = it))) },
        )
        DetailsField(
            value = editor.address,
            label = taffyString(R.string.taffy_details_address),
            testTag = DETAILS_ADDRESS_TEST_TAG,
            onChange = { onIntent(SavedDetailsIntent.EditorChanged(editor.copy(address = it))) },
        )
        DetailsField(
            value = editor.postcode,
            label = taffyString(R.string.taffy_details_postcode),
            testTag = DETAILS_POSTCODE_TEST_TAG,
            onChange = { onIntent(SavedDetailsIntent.EditorChanged(editor.copy(postcode = it))) },
        )
        DetailsField(
            value = editor.country,
            label = taffyString(R.string.taffy_details_country),
            testTag = DETAILS_COUNTRY_TEST_TAG,
            onChange = { onIntent(SavedDetailsIntent.EditorChanged(editor.copy(country = it))) },
        )
        TaffyPrimaryButton(
            label = taffyString(R.string.taffy_details_save),
            onClick = { onIntent(SavedDetailsIntent.Save) },
            modifier = Modifier.fillMaxWidth(),
            testTag = DETAILS_SAVE_TEST_TAG,
        )
        if (!adding) {
            TaffyDangerButton(
                label = taffyString(R.string.taffy_details_delete),
                onClick = { onIntent(SavedDetailsIntent.Delete) },
                modifier = Modifier.fillMaxWidth(),
                testTag = DETAILS_DELETE_TEST_TAG,
            )
        }
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_details_cancel),
            onClick = { onIntent(SavedDetailsIntent.DismissEditor) },
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
private fun DetailsField(
    value: String,
    label: String,
    testTag: String,
    onChange: (String) -> Unit,
    keyboard: KeyboardType = KeyboardType.Text,
) {
    OutlinedTextField(
        value = value,
        onValueChange = onChange,
        label = { Text(label) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = keyboard),
        shape = TaffyTheme.shapes.card,
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = TaffyTheme.colors.surfaceRaised,
            unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
        ),
        modifier = Modifier
            .fillMaxWidth()
            .testTag(testTag),
    )
}

const val DETAILS_EDITOR_TEST_TAG: String = "saved_details_editor"
const val DETAILS_GIVEN_TEST_TAG: String = "saved_details_given"
const val DETAILS_FAMILY_TEST_TAG: String = "saved_details_family"
const val DETAILS_EMAIL_TEST_TAG: String = "saved_details_email"
const val DETAILS_PHONE_TEST_TAG: String = "saved_details_phone"
const val DETAILS_ADDRESS_TEST_TAG: String = "saved_details_address"
const val DETAILS_POSTCODE_TEST_TAG: String = "saved_details_postcode"
const val DETAILS_COUNTRY_TEST_TAG: String = "saved_details_country"
const val DETAILS_SAVE_TEST_TAG: String = "saved_details_save"
const val DETAILS_DELETE_TEST_TAG: String = "saved_details_delete"
const val DETAILS_DELETE_CONFIRM_TEST_TAG: String = "saved_details_delete_confirm"
