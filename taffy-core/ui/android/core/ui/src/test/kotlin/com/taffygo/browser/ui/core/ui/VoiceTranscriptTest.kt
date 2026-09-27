// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import java.nio.charset.StandardCharsets
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class VoiceTranscriptTest {
    @Test
    fun `blank recognition is not a transcript`() {
        assertNull(VoiceTranscript.bounded(" \n\t "))
    }

    @Test
    fun `ordinary recognition is trimmed but not marked incomplete`() {
        val transcript = requireNotNull(VoiceTranscript.bounded("  ask about this page  "))

        assertEquals("ask about this page", transcript.text)
        assertFalse(transcript.wasTruncated)
        assertFalse(transcript.toString().contains("ask about this page"))
    }

    @Test
    fun `oversized unicode recognition is bounded on a whole character`() {
        val unit = "किताब📚"
        val transcript = VoiceTranscript.bounded(unit.repeat(MAX_VOICE_TRANSCRIPT_BYTES))

        assertNotNull(transcript)
        transcript ?: return
        assertTrue(transcript.wasTruncated)
        assertTrue(
            transcript.text.toByteArray(StandardCharsets.UTF_8).size <=
                MAX_VOICE_TRANSCRIPT_BYTES,
        )
        assertFalse(transcript.text.last().isHighSurrogate())
        if (transcript.text.last().isLowSurrogate()) {
            assertTrue(transcript.text.length > 1)
            assertTrue(transcript.text[transcript.text.lastIndex - 1].isHighSurrogate())
        }
    }
}
