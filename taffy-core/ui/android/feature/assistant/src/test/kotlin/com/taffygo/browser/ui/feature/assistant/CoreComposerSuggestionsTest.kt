// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The composer's request lifecycle: it waits for typing to stop, asks once, and
 * shows only the answer to the request it is on.
 *
 * The opt-in is for [UnconfinedTestDispatcher], which these tests need rather
 * than prefer: the push channel drops what it emits at nobody, so a collector
 * that has not subscribed yet would turn every assertion below into a
 * tautology.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class CoreComposerSuggestionsTest {

    private val core = RecordingComposerCoreApiClient()
    private val minted = ArrayDeque(listOf("typing-1", "typing-2", "typing-3"))

    private fun repository() = CoreComposerSuggestions(
        core = core,
        debounceMillis = DEBOUNCE,
        mintRequestId = { minted.removeFirst() },
    )

    @Test
    fun `nothing is asked for until typing stops`() = runTest {
        val typed = composing()
        val offered = mutableListOf<ComposerSuggestion>()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect { offered += it }
        }

        typed.value = at("the qu")
        advanceTimeBy(DEBOUNCE - 1)
        runCurrent()
        assertTrue(core.composerRequests.isEmpty())

        advanceTimeBy(2)
        runCurrent()
        assertEquals(
            listOf(
                RecordingComposerCoreApiClient.ComposerRequest("typing-1", "the qu", null),
            ),
            core.composerRequests,
        )
    }

    @Test
    fun `what stands after the caret is sent with what stands before it`() = runTest {
        val typed = composing()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect {}
        }

        typed.value = ComposerCaretText(prefix = "the quick", suffix = " fox")
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        // The half the seam has always carried and never been given: a model
        // asked to continue "the quick" can now see it ends in "fox".
        assertEquals(
            listOf(
                RecordingComposerCoreApiClient.ComposerRequest("typing-1", "the quick", " fox"),
            ),
            core.composerRequests,
        )
    }

    @Test
    fun `a range of text selected is never a request`() = runTest {
        val typed = composing(at("the qu"))
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect {}
        }
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        // No single insertion point, so nothing to complete at.
        typed.value = null
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        assertEquals(listOf("the qu"), core.composerRequests.map { it.prefix })
    }

    @Test
    fun `keystrokes inside the window cost one request, not one each`() = runTest {
        val typed = composing()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect {}
        }

        typed.value = at("t")
        advanceTimeBy(100)
        typed.value = at("th")
        advanceTimeBy(100)
        typed.value = at("the")
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        assertEquals(1, core.composerRequests.size)
        assertEquals("the", core.composerRequests.single().prefix)
    }

    @Test
    fun `an answer to a superseded request is discarded rather than shown`() = runTest {
        val typed = composing()
        val offered = mutableListOf<ComposerSuggestion>()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect { offered += it }
        }

        typed.value = at("the qu")
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()
        typed.value = at("the quick br")
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        // The first request is still in flight — a request cannot be called
        // back — and its answer lands after the composer has moved on.
        core.deliver("typing-1", "ery is slow")
        runCurrent()
        assertTrue(offered.none { it == ComposerSuggestion.Ghost("ery is slow") })

        core.deliver("typing-2", "own fox")
        runCurrent()
        assertEquals(ComposerSuggestion.Ghost("own fox"), offered.last())
    }

    @Test
    fun `absent text is offered as nothing, and empty text as its own answer`() = runTest {
        val typed = composing()
        val offered = mutableListOf<ComposerSuggestion>()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect { offered += it }
        }

        typed.value = at("the qu")
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()
        core.deliver("typing-1", null)
        runCurrent()
        assertEquals(ComposerSuggestion.None, offered.last())

        typed.value = at("the quick")
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()
        core.deliver("typing-2", "")
        runCurrent()
        // Not the same fact, and neither of them a failure.
        assertEquals(ComposerSuggestion.NoCharacters, offered.last())
    }

    @Test
    fun `an empty composer is never a request`() = runTest {
        val typed = composing(at("the qu"))
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect {}
        }
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        // Nothing before the caret is nothing to continue, whatever stands
        // after it — and it is what the core refuses too.
        typed.value = ComposerCaretText(prefix = "   ", suffix = "refundable?")
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        assertEquals(listOf("the qu"), core.composerRequests.map { it.prefix })
    }

    /**
     * A composer that stops wanting an answer says so, rather than abandoning
     * the request and paying for it (decision 0097 section 3). Superseding
     * needs no help — a newer identity displaces the old one and the browser
     * stops it — so this is the half only the surface can state.
     */
    @Test
    fun `a composer that stops wanting an answer withdraws the request`() = runTest {
        val typed = composing(at("the qu"))
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect {}
        }
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()
        val asked = core.composerRequests.single().requestId

        // The caret moved out of the composer, so there is no insertion point
        // and nothing left to complete at.
        typed.value = null
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        assertEquals(listOf(asked), core.withdrawnRequests)
    }

    /**
     * An answer that already landed is nothing to stop, and a withdrawal for it
     * would name a request the core has finished with.
     */
    @Test
    fun `an answered request is not withdrawn when the composer moves on`() = runTest {
        val typed = composing(at("the qu"))
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect {}
        }
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()
        core.deliver(core.composerRequests.single().requestId, "ick brown fox")
        runCurrent()

        typed.value = null
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        assertEquals(emptyList<String>(), core.withdrawnRequests)
    }

    @Test
    fun `a core that refuses the request offers nothing rather than a failure`() = runTest {
        core.refusesComposerRequests = true
        val typed = composing()
        val offered = mutableListOf<ComposerSuggestion>()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            repository().suggestions(typed).collect { offered += it }
        }

        typed.value = at("the qu")
        advanceTimeBy(DEBOUNCE + 1)
        runCurrent()

        assertEquals(ComposerSuggestion.None, offered.last())
    }

    /** A composer whose caret sits at the end of [text]. */
    private fun at(text: String) = ComposerCaretText(prefix = text, suffix = null)

    /** What the surface pushes: both halves, or null for no insertion point. */
    private fun composing(first: ComposerCaretText? = at("")) = MutableStateFlow(first)

    private companion object {
        const val DEBOUNCE = 350L
    }
}
