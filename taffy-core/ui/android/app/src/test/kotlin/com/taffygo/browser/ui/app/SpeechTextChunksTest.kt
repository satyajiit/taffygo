// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SpeechTextChunksTest {
    @Test
    fun chunksPreserveExactTextWithinPlatformLimit() {
        val text = "First sentence. दूसरा वाक्य 📚 and a final phrase."

        val chunks = speechTextChunks(text, maxLength = 12)

        assertEquals(text, chunks.joinToString(separator = ""))
        assertTrue(chunks.all { it.length <= 12 })
        assertTrue(chunks.none { it.last().isHighSurrogate() })
        assertTrue(chunks.none { it.first().isLowSurrogate() })
    }

    @Test
    fun emptyTextHasNoPlatformWork() {
        assertTrue(speechTextChunks("", maxLength = 4).isEmpty())
    }

    @Test
    fun surrogatePairsRemainWholeAtTheHardLimit() {
        val text = "😀😀😀"

        val chunks = speechTextChunks(text, maxLength = 2)

        assertEquals(text, chunks.joinToString(separator = ""))
        assertTrue(chunks.all { it.length == 2 })
    }
}
