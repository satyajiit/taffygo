// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.VoiceEntryState
import com.taffygo.browser.ui.core.ui.VoiceInputFailure
import com.taffygo.browser.ui.core.ui.taffyString

/** SCR-709 reached from the focused address box. Review never commits the reading. */
@Composable
internal fun AddressVoiceInputOverlay(
    state: VoiceEntryState,
    onIntent: (AddressBarIntent) -> Unit,
) {
    if (state == VoiceEntryState.Closed) return
    AlertDialog(
        onDismissRequest = { onIntent(AddressBarIntent.CancelVoiceInput) },
        title = { Text(text = taffyString(voiceTitle(state))) },
        text = {
            Column(
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                modifier = Modifier
                    .heightIn(max = 240.dp)
                    .verticalScroll(rememberScrollState())
                    .testTag(ADDRESS_VOICE_OVERLAY_TEST_TAG),
            ) {
                Text(
                    text = taffyString(voiceBody(state)),
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textPrimary,
                    modifier = Modifier
                        .semantics { liveRegion = LiveRegionMode.Polite }
                        .testTag(ADDRESS_VOICE_STATUS_TEST_TAG),
                )
                voiceTranscript(state)?.let { transcript ->
                    Text(
                        text = transcript.text,
                        style = TaffyTheme.typography.body,
                        color = TaffyTheme.colors.textPrimary,
                        modifier = Modifier.testTag(ADDRESS_VOICE_TRANSCRIPT_TEST_TAG),
                    )
                    if (transcript.wasTruncated) {
                        Text(
                            text = taffyString(R.string.taffy_address_voice_truncated),
                            style = TaffyTheme.typography.detail,
                            color = TaffyTheme.colors.caution,
                        )
                    }
                }
                Text(
                    text = taffyString(R.string.taffy_address_voice_privacy),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.testTag(ADDRESS_VOICE_PRIVACY_TEST_TAG),
                )
            }
        },
        confirmButton = {
            if (state is VoiceEntryState.Review) {
                TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_address_voice_use),
                    onClick = { onIntent(AddressBarIntent.ConfirmVoiceInput) },
                    testTag = ADDRESS_VOICE_CONFIRM_TEST_TAG,
                )
            }
        },
        dismissButton = {
            TaffySecondaryButton(
                label = taffyString(
                    if (state is VoiceEntryState.Error) {
                        R.string.taffy_address_voice_close
                    } else {
                        R.string.taffy_address_voice_cancel
                    },
                ),
                onClick = { onIntent(AddressBarIntent.CancelVoiceInput) },
                testTag = ADDRESS_VOICE_CANCEL_TEST_TAG,
            )
        },
    )
}

private fun voiceTitle(state: VoiceEntryState): Int = when (state) {
    VoiceEntryState.Closed,
    VoiceEntryState.RequestingPermission,
    -> R.string.taffy_address_voice_title
    is VoiceEntryState.Listening -> R.string.taffy_address_voice_listening_title
    is VoiceEntryState.Processing -> R.string.taffy_address_voice_processing_title
    is VoiceEntryState.Review -> R.string.taffy_address_voice_review_title
    is VoiceEntryState.Error -> R.string.taffy_address_voice_error_title
}

private fun voiceBody(state: VoiceEntryState): Int = when (state) {
    VoiceEntryState.Closed -> R.string.taffy_address_voice_ready
    VoiceEntryState.RequestingPermission -> R.string.taffy_address_voice_permission
    is VoiceEntryState.Listening -> R.string.taffy_address_voice_listening
    is VoiceEntryState.Processing -> R.string.taffy_address_voice_processing
    is VoiceEntryState.Review -> R.string.taffy_address_voice_review
    is VoiceEntryState.Error -> voiceFailureBody(state.reason)
}

private fun voiceFailureBody(reason: VoiceInputFailure): Int = when (reason) {
    VoiceInputFailure.PERMISSION_DENIED -> R.string.taffy_address_voice_permission_denied
    VoiceInputFailure.UNAVAILABLE -> R.string.taffy_address_voice_unavailable
    VoiceInputFailure.NO_SPEECH -> R.string.taffy_address_voice_no_speech
    VoiceInputFailure.BUSY -> R.string.taffy_address_voice_busy
    VoiceInputFailure.CONNECTION -> R.string.taffy_address_voice_connection
    VoiceInputFailure.TOO_LONG -> R.string.taffy_address_voice_too_long
    VoiceInputFailure.OTHER -> R.string.taffy_address_voice_failed
}

private fun voiceTranscript(state: VoiceEntryState) = when (state) {
    is VoiceEntryState.Listening -> state.partial
    is VoiceEntryState.Processing -> state.partial
    is VoiceEntryState.Review -> state.transcript
    else -> null
}

const val ADDRESS_VOICE_OVERLAY_TEST_TAG: String = "address_voice_overlay"
const val ADDRESS_VOICE_STATUS_TEST_TAG: String = "address_voice_status"
const val ADDRESS_VOICE_TRANSCRIPT_TEST_TAG: String = "address_voice_transcript"
const val ADDRESS_VOICE_PRIVACY_TEST_TAG: String = "address_voice_privacy"
const val ADDRESS_VOICE_CONFIRM_TEST_TAG: String = "address_voice_confirm"
const val ADDRESS_VOICE_CANCEL_TEST_TAG: String = "address_voice_cancel"
