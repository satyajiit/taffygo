// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Modifier
import androidx.compose.ui.autofill.ContentType
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentType
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskChallengeKind
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import com.taffygo.browser.ui.core.ui.taffyString
import kotlinx.coroutines.withContext

/**
 * One row of the form, drawn entirely from what the browser described.
 *
 * There is no `when` over field *names* here and there must never be one: the
 * next site calls the same thing something else, and a product that recognised
 * a name would work on exactly the sites somebody had already met. Everything
 * that differs between rows — the keyboard, the masking, the autofill hint, the
 * picture — is decided from [TaskChallengeKind] and from the two flags beside
 * it.
 */
@Composable
internal fun TaskInputFieldRow(
    row: TaskInputUiState.Row,
    host: String,
    onValueChange: (String) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        if (row.challenge == TaskChallengeKind.IMAGE_CHALLENGE) {
            TaskInputChallengeImage(row = row, host = host)
        }
        val oneTimeCode = row.challenge == TaskChallengeKind.ONE_TIME_CODE
        OutlinedTextField(
            value = row.value,
            onValueChange = onValueChange,
            label = { Text(text = row.label) },
            placeholder = row.placeholder?.let { hint -> { Text(text = hint) } },
            singleLine = true,
            // Masked because the browser said the field is sensitive, never
            // because of what it is called. A site that marks a code sensitive
            // gets masking; one that does not, does not — and the decision
            // stays with the party that can see the page.
            visualTransformation = if (row.sensitive) {
                PasswordVisualTransformation()
            } else {
                VisualTransformation.None
            },
            keyboardOptions = KeyboardOptions(
                keyboardType = when {
                    // NumberPassword rather than Number: it is the numeric
                    // keyboard with the platform's own no-suggestions,
                    // no-clipboard-learning behaviour, which is what a code
                    // that arrived by message should get.
                    oneTimeCode -> KeyboardType.NumberPassword
                    row.sensitive -> KeyboardType.Password
                    else -> KeyboardType.Text
                },
                imeAction = ImeAction.Done,
            ),
            modifier = Modifier
                .fillMaxWidth()
                .testTag("$TASK_INPUT_FIELD_TEST_TAG_PREFIX${row.id}")
                .then(
                    // The platform's own one-time-code path: the keyboard
                    // offers the code out of the message and nothing else in
                    // this process ever reads the message.
                    if (oneTimeCode) {
                        Modifier.semantics { contentType = ContentType.SmsOtpCode }
                    } else {
                        Modifier
                    },
                ),
        )
    }
}

/**
 * The picture a site wants read back, drawn from bytes the browser fetched.
 *
 * Bytes, never an address. A picture this process fetched for itself would be a
 * request to the site from outside everything the browser decided about that
 * page — its cookies, its filtering, its origin rules — for the sake of drawing
 * one image. A body that will not decode draws nothing rather than crashing:
 * the row's field is still there, and a person can still see there is a
 * challenge they cannot read.
 */
@Composable
private fun TaskInputChallengeImage(row: TaskInputUiState.Row, host: String) {
    val imageBytes = row.challengeImage?.takeIf { it.isNotEmpty() }
    val decodeDispatcher = LocalAppDispatchers.current.default
    val initialState = if (imageBytes == null) {
        ChallengeImageState.Unreadable
    } else {
        ChallengeImageState.Loading
    }
    val state by produceState<ChallengeImageState>(
        initialValue = initialState,
        key1 = imageBytes,
        key2 = decodeDispatcher,
    ) {
        if (imageBytes == null) {
            value = ChallengeImageState.Unreadable
        } else {
            val picture = withContext(decodeDispatcher) {
                decodeTaskInputChallengeImage(imageBytes)
            }
            value = picture?.let(ChallengeImageState::Ready)
                ?: ChallengeImageState.Unreadable
        }
    }
    when (val current = state) {
        ChallengeImageState.Loading -> Unit
        ChallengeImageState.Unreadable -> Text(
            text = taffyString(R.string.taffy_task_input_image_unreadable),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.caution,
            modifier = Modifier.testTag(TASK_INPUT_IMAGE_UNREADABLE_TEST_TAG),
        )
        is ChallengeImageState.Ready -> Image(
            bitmap = current.picture,
            contentDescription = taffyString(R.string.taffy_task_input_image_description, host),
            contentScale = ContentScale.Fit,
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(max = ChallengeImageMaxHeight)
                .testTag(TASK_INPUT_IMAGE_TEST_TAG),
        )
    }
}

private sealed interface ChallengeImageState {
    data object Loading : ChallengeImageState

    data object Unreadable : ChallengeImageState

    data class Ready(val picture: ImageBitmap) : ChallengeImageState
}

private val ChallengeImageMaxHeight = 120.dp

/** The tags the form sheet's field rows carry. */
const val TASK_INPUT_FIELD_TEST_TAG_PREFIX: String = "task_input_field_"
const val TASK_INPUT_IMAGE_TEST_TAG: String = "task_input_image"
const val TASK_INPUT_IMAGE_UNREADABLE_TEST_TAG: String = "task_input_image_unreadable"
