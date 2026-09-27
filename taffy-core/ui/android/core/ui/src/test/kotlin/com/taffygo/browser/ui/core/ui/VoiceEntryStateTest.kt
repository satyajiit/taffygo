// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

class VoiceEntryStateTest {
    @Test
    fun `final words stop at review and do not imply confirmation`() {
        val words = requireNotNull(VoiceTranscript.bounded("find red shoes"))

        val state = voiceEntryAfterEvent(
            VoiceEntryState.Listening(words),
            VoiceInputEvent.Ready(words),
        )

        assertEquals(VoiceEntryState.Review(words), state)
    }

    @Test
    fun `processing keeps the bounded partial words visible`() {
        val partial = requireNotNull(VoiceTranscript.bounded("find red"))

        val state = voiceEntryAfterEvent(
            VoiceEntryState.Listening(partial),
            VoiceInputEvent.Processing,
        )

        assertEquals(VoiceEntryState.Processing(partial), state)
    }

    @Test
    fun `permission failure is typed`() {
        val state = voiceEntryAfterEvent(
            VoiceEntryState.RequestingPermission,
            VoiceInputEvent.Failed(VoiceInputFailure.PERMISSION_DENIED),
        )

        assertSame(
            VoiceInputFailure.PERMISSION_DENIED,
            (state as VoiceEntryState.Error).reason,
        )
    }
}
