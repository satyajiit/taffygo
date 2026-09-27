// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/** Events from the platform adapter. None of them changes an editor by itself. */
sealed interface VoiceInputEvent {
    /** Android is asking the person for microphone access. */
    data object PermissionRequested : VoiceInputEvent

    /** The microphone is active and the person can speak. */
    data object Listening : VoiceInputEvent

    /** Unconfirmed words shown only inside the listening overlay. */
    data class Partial(val transcript: VoiceTranscript) : VoiceInputEvent

    /** Speech ended and the device service is preparing its final words. */
    data object Processing : VoiceInputEvent

    /** Final words ready for a separate, explicit person confirmation. */
    data class Ready(val transcript: VoiceTranscript) : VoiceInputEvent

    /** A terminal failure. The feature turns the typed reason into local copy. */
    data class Failed(val reason: VoiceInputFailure) : VoiceInputEvent
}
