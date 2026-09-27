// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * Transient presentation state shared by editors that accept confirmed speech.
 *
 * [Review] is intentionally a different state from every state in which the
 * microphone is active. A feature can copy its transcript into an editor only
 * in response to its own confirm intent; no platform event performs that copy.
 */
sealed interface VoiceEntryState {
    data object Closed : VoiceEntryState
    data object RequestingPermission : VoiceEntryState
    data class Listening(val partial: VoiceTranscript? = null) : VoiceEntryState
    data class Processing(val partial: VoiceTranscript? = null) : VoiceEntryState
    data class Review(val transcript: VoiceTranscript) : VoiceEntryState
    data class Error(val reason: VoiceInputFailure) : VoiceEntryState
}

/** Pure projection of one platform callback. It never returns confirmed editor text. */
fun voiceEntryAfterEvent(
    current: VoiceEntryState,
    event: VoiceInputEvent,
): VoiceEntryState = when (event) {
    VoiceInputEvent.PermissionRequested -> VoiceEntryState.RequestingPermission
    VoiceInputEvent.Listening -> VoiceEntryState.Listening()
    is VoiceInputEvent.Partial -> VoiceEntryState.Listening(event.transcript)
    VoiceInputEvent.Processing -> VoiceEntryState.Processing(
        partial = when (current) {
            is VoiceEntryState.Listening -> current.partial
            is VoiceEntryState.Processing -> current.partial
            else -> null
        },
    )
    is VoiceInputEvent.Ready -> VoiceEntryState.Review(event.transcript)
    is VoiceInputEvent.Failed -> VoiceEntryState.Error(event.reason)
}
