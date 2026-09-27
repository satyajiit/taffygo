// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.api.ComposerCompletionReport
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Decision 0097 section 3, on its own: one request is current, a newer identity
 * supersedes an older one, and an answer the composer has moved past is
 * discarded on arrival rather than shown.
 */
class ComposerSuggestionTurnTest {

    @Test
    fun `an answer to the request in hand is offered`() {
        val turn = ComposerSuggestionTurn()
            .asked("typing-1")
            .answered(ComposerCompletionReport("typing-1", " fox"))

        assertEquals(ComposerSuggestion.Ghost(" fox"), turn.offered)
        assertFalse(turn.waiting)
    }

    @Test
    fun `an answer to a superseded request is discarded, not shown`() {
        val turn = ComposerSuggestionTurn()
            .asked("typing-1")
            .asked("typing-2")
            // The person kept typing, so the composer moved on. The answer to
            // typing-1 still arrives — a request in flight is not unasked — and
            // it must not appear over the sentence that replaced it.
            .answered(ComposerCompletionReport("typing-1", "stale"))

        assertEquals(ComposerSuggestion.None, turn.offered)
        assertEquals("typing-2", turn.awaiting)

        // The newer one is what lands.
        val settled = turn.answered(ComposerCompletionReport("typing-2", "fresh"))
        assertEquals(ComposerSuggestion.Ghost("fresh"), settled.offered)
    }

    @Test
    fun `superseding drops whatever was standing`() {
        val standing = ComposerSuggestionTurn()
            .asked("typing-1")
            .answered(ComposerCompletionReport("typing-1", " fox"))

        // One more word is a different sentence; the offer for the old one goes
        // with it rather than hanging over the new one until an answer comes.
        assertEquals(ComposerSuggestion.None, standing.asked("typing-2").offered)
    }

    @Test
    fun `an answer arriving after the composer stopped waiting is discarded`() {
        val forgotten = ComposerSuggestionTurn()
            .asked("typing-1")
            .forgotten()
            .answered(ComposerCompletionReport("typing-1", "late"))

        assertNull(forgotten.awaiting)
        assertEquals(ComposerSuggestion.None, forgotten.offered)
    }

    @Test
    fun `no suggestion and a suggestion of nothing take different paths`() {
        val nothingOffered = ComposerSuggestionTurn()
            .asked("typing-1")
            .answered(ComposerCompletionReport("typing-1", text = null))
        val emptyOffered = ComposerSuggestionTurn()
            .asked("typing-1")
            .answered(ComposerCompletionReport("typing-1", text = ""))

        assertEquals(ComposerSuggestion.None, nothingOffered.offered)
        assertEquals(ComposerSuggestion.NoCharacters, emptyOffered.offered)
        assertNotEquals(nothingOffered.offered, emptyOffered.offered)

        // Neither is an error and neither has characters to draw over the cursor.
        assertNull((nothingOffered.offered as? ComposerSuggestion.Ghost)?.text)
        assertNull((emptyOffered.offered as? ComposerSuggestion.Ghost)?.text)
    }

    @Test
    fun `the three answers are read off the report's text`() {
        assertEquals(ComposerSuggestion.None, ComposerSuggestion.of(null))
        assertEquals(ComposerSuggestion.NoCharacters, ComposerSuggestion.of(""))
        assertEquals(ComposerSuggestion.Ghost("x"), ComposerSuggestion.of("x"))
        assertTrue(ComposerSuggestion.of(" ") is ComposerSuggestion.Ghost)
    }
}
