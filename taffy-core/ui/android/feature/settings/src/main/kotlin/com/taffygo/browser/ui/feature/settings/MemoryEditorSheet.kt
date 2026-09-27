// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Add or edit a Memory note. */
@Composable
internal fun MemoryEditorSheet(
    state: MemoryUiState,
    onIntent: (MemoryIntent) -> Unit,
) {
    val editor = state.editor ?: return
    val adding = editor.id == null
    TaffyBottomSheet(
        title = taffyString(
            if (adding) {
                R.string.taffy_memory_editor_add_title
            } else {
                R.string.taffy_memory_editor_edit_title
            },
        ),
        onDismissRequest = { onIntent(MemoryIntent.DismissEditor) },
        testTag = MEMORY_EDITOR_TEST_TAG,
    ) {
        state.mutationFailure?.let { failure ->
            Text(
                text = memoryMutationFailureMessage(failure),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.dangerText,
                modifier = Modifier.testTag(MEMORY_MUTATION_FAILURE_TEST_TAG),
            )
        }
        if (state.confirmDelete) {
            Text(
                text = taffyString(R.string.taffy_memory_delete_title),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = taffyString(R.string.taffy_memory_delete_body),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textSecondary,
            )
            TaffyDangerButton(
                label = taffyString(R.string.taffy_memory_delete_confirm),
                onClick = { onIntent(MemoryIntent.ConfirmDelete) },
                modifier = Modifier.fillMaxWidth(),
                enabled = !state.mutationInFlight,
                loading = state.mutationInFlight,
                testTag = MEMORY_DELETE_CONFIRM_TEST_TAG,
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_memory_delete_keep),
                onClick = { onIntent(MemoryIntent.CancelDelete) },
                modifier = Modifier.fillMaxWidth(),
            )
            return@TaffyBottomSheet
        }
        OutlinedTextField(
            value = editor.text,
            onValueChange = { onIntent(MemoryIntent.ChangeText(it)) },
            enabled = !state.mutationInFlight,
            label = { Text(taffyString(R.string.taffy_memory_field_label)) },
            supportingText = { Text(taffyString(R.string.taffy_memory_field_caption)) },
            shape = TaffyTheme.shapes.card,
            colors = OutlinedTextFieldDefaults.colors(
                focusedContainerColor = TaffyTheme.colors.surfaceRaised,
                unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
            ),
            modifier = Modifier
                .fillMaxWidth()
                .testTag(MEMORY_FIELD_TEST_TAG),
        )
        editor.whyNote?.let { note ->
            Text(
                text = memoryWhyLine(note),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        TaffyPrimaryButton(
            label = taffyString(R.string.taffy_memory_save),
            onClick = { onIntent(MemoryIntent.Save) },
            enabled = editor.text.isNotBlank() && !state.mutationInFlight,
            loading = state.mutationInFlight,
            modifier = Modifier.fillMaxWidth(),
            testTag = MEMORY_SAVE_TEST_TAG,
        )
        if (!adding) {
            TaffyDangerButton(
                label = taffyString(R.string.taffy_memory_delete),
                onClick = { onIntent(MemoryIntent.Delete) },
                modifier = Modifier.fillMaxWidth(),
                enabled = !state.mutationInFlight,
                testTag = MEMORY_DELETE_TEST_TAG,
            )
        }
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_memory_cancel),
            onClick = { onIntent(MemoryIntent.DismissEditor) },
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
private fun memoryMutationFailureMessage(failure: MemoryUiState.MutationFailure): String {
    val outOfDate = failure.reason == FailureReason.STALE_REVISION ||
        failure.reason == FailureReason.NOT_FOUND
    return when (failure.operation) {
        MemoryUiState.MutationOperation.SAVE -> taffyString(
            if (outOfDate) {
                R.string.taffy_memory_save_out_of_date
            } else {
                R.string.taffy_memory_save_failed
            },
        )
        MemoryUiState.MutationOperation.DELETE -> taffyString(
            if (outOfDate) {
                R.string.taffy_memory_delete_out_of_date
            } else {
                R.string.taffy_memory_delete_failed
            },
        )
    }
}

const val MEMORY_EDITOR_TEST_TAG: String = "memory_editor"
const val MEMORY_FIELD_TEST_TAG: String = "memory_field"
const val MEMORY_SAVE_TEST_TAG: String = "memory_save"
const val MEMORY_DELETE_TEST_TAG: String = "memory_delete"
const val MEMORY_DELETE_CONFIRM_TEST_TAG: String = "memory_delete_confirm"
const val MEMORY_MUTATION_FAILURE_TEST_TAG: String = "memory_mutation_failure"
