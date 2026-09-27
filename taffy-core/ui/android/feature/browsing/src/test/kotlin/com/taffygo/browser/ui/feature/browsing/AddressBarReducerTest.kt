// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import com.taffygo.browser.ui.core.ui.TaffyDestination
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-103's reducer. The resolution itself belongs to the data layer, so
 * these tests hand in a resolver and check what the screen does with the answer.
 */
class AddressBarReducerTest {

    @Test
    fun `typing puts the reading and the rows into the state`() {
        val state = reduceAddressBar(
            state = AddressBarUiState(),
            intent = AddressBarIntent.InputChanged("docs.example.test"),
            resolve = { AddressBarInterpretation.GoTo(it, it) },
            suggest = { listOf(Suggestion("s0", it, AddressBarInterpretation.GoTo(it, it))) },
        )

        assertEquals("docs.example.test", state.input)
        assertTrue(state.interpretation is AddressBarInterpretation.GoTo)
        assertEquals(1, state.suggestions.size)
    }

    @Test
    fun `an empty box has no reading at all`() {
        val state = reduceAddressBar(
            state = AddressBarUiState(input = "something"),
            intent = AddressBarIntent.InputChanged("   "),
            resolve = { AddressBarInterpretation.Search(it) },
            suggest = { emptyList() },
        )

        assertNull(state.interpretation)
        assertTrue(state.suggestions.isEmpty())
    }

    @Test
    fun `an errand reading is decided by the start rule and the others are not`() {
        val errand = reduceAddressBar(
            state = AddressBarUiState(conditions = ready),
            intent = AddressBarIntent.InputChanged("download my aadhaar"),
            resolve = { AddressBarInterpretation.TaskForTaffy(it, TaskTemplate.WEB_ERRAND) },
            suggest = { emptyList() },
        )
        val search = reduceAddressBar(
            state = AddressBarUiState(conditions = ready),
            intent = AddressBarIntent.InputChanged("tv price"),
            resolve = { AddressBarInterpretation.Search(it) },
            suggest = { emptyList() },
        )

        val start = errand.start as TaskStartDecision.Start
        assertEquals("download my aadhaar", start.request.goal)
        assertEquals(TaskTemplate.WEB_ERRAND, start.request.template)
        assertTrue(start.request.consent.sourceDiscoveryEnabled)
        assertNull(search.start)
    }

    /**
     * Sending a job that starts here keeps the words, because the panel under
     * the box is about them; a job the rule refuses keeps them too, so the row
     * can say why without asking for the request twice.
     */
    @Test
    fun `choosing a reading that starts here keeps the words and marks the start`() {
        val errand = reduceAddressBar(
            state = AddressBarUiState(conditions = ready, menuOpen = true),
            intent = AddressBarIntent.InputChanged("download my aadhaar"),
            resolve = ::errand,
            suggest = { emptyList() },
        )
        val reading = checkNotNull(errand.reading)

        val sent = reduceAddressBar(errand, AddressBarIntent.Choose(reading), ::errand) { emptyList() }
        assertEquals("download my aadhaar", sent.input)
        assertTrue(sent.starting)
        assertFalse(sent.menuOpen)

        val notSetUp = errand.copy(conditions = ready.copy(readiness = TaffyReadiness.NotSetUp))
        assertEquals(TaskStartDecision.Refused(TaskStartRefusal.SETUP_NEEDED), notSetUp.start)
        val kept = reduceAddressBar(notSetUp, AddressBarIntent.Choose(reading), ::errand) { emptyList() }
        assertEquals("download my aadhaar", kept.input)
        assertFalse(kept.starting)

        // The conditions outlive the words: an emptied box still knows the
        // core is ready, so the next request is not refused for a stale reason.
        val left = reduceAddressBar(errand, AddressBarIntent.Left, ::errand) { emptyList() }
        assertEquals("", left.input)
        assertEquals(ready, left.conditions)
    }

    /**
     * An ended task's panel has two doors. Try again is the reading chosen a
     * second time, with the start rule asked afresh; home is leaving, and it
     * takes the words with the panel.
     */
    @Test
    fun `an ended task offers the same request again or the start page back`() {
        val errand = reduceAddressBar(
            state = AddressBarUiState(conditions = ready),
            intent = AddressBarIntent.InputChanged("download my aadhaar"),
            resolve = ::errand,
            suggest = { emptyList() },
        )
        val ended = errand.copy(started = StartedTask("task-1", "download my aadhaar"))

        val again = reduceAddressBar(ended, AddressBarIntent.TryAgain, ::errand) { emptyList() }
        assertNull(again.started)
        assertTrue(again.starting)
        assertEquals("download my aadhaar", again.input)

        // Refused now — nothing is set up any more — the box comes back with
        // the row under it saying why, and nothing is marked as starting.
        val notSetUp = ended.copy(conditions = ready.copy(readiness = TaffyReadiness.NotSetUp))
        val refused = reduceAddressBar(notSetUp, AddressBarIntent.TryAgain, ::errand) { emptyList() }
        assertNull(refused.started)
        assertFalse(refused.starting)
        assertEquals("download my aadhaar", refused.input)

        // With no task ended there is nothing to try again.
        assertEquals(errand, reduceAddressBar(errand, AddressBarIntent.TryAgain, ::errand) { emptyList() })

        val home = reduceAddressBar(ended, AddressBarIntent.LeaveTask, ::errand) { emptyList() }
        assertNull(home.started)
        assertEquals("", home.input)
        assertEquals(ready, home.conditions)
    }

    /** A question on a blank tab is an errand; over a page it is an ask about the page. */
    @Test
    fun `a question starts here only on a blank tab`() {
        val question = reduceAddressBar(
            state = AddressBarUiState(conditions = ready.copy(onBlankTab = true)),
            intent = AddressBarIntent.InputChanged("how do I renew a passport?"),
            resolve = { AddressBarInterpretation.AskTaffy(it) },
            suggest = { emptyList() },
        )
        assertTrue(question.start is TaskStartDecision.Start)

        val overPage = question.copy(conditions = ready.copy(onBlankTab = false))
        assertNull(overPage.start)
    }

    @Test
    fun `committing a reading takes what was typed with it`() {
        val typed = typed("docs.example.test")

        val committed = reduceAddressBar(
            state = typed,
            intent = AddressBarIntent.Choose(requireNotNull(typed.interpretation)),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        // The reading has been acted on, so the draft that produced it is spent.
        // Anything left here is a sentence the person has already finished.
        assertEquals("", committed.input)
        assertNull(committed.interpretation)
        assertTrue(committed.suggestions.isEmpty())
    }

    @Test
    fun `giving up on the box gives up on the draft too`() {
        val typed = typed("docs.example.test")

        val dismissed = reduceAddressBar(
            state = typed,
            intent = AddressBarIntent.Dismiss,
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        assertEquals("", dismissed.input)
        assertNull(dismissed.interpretation)
    }

    /**
     * The sequence from the phone, in the order a person did it.
     *
     * They typed an address on one tab and went to it. Later — after tabs had
     * been closed and another opened — they opened the box again and asked a
     * question. What came out was "wikipedia.orgwhat is a tapir": the field
     * still held the committed address, and a keyboard types onto the end of
     * whatever a field already holds. Two things have to be true for that to be
     * impossible, and both are asserted here — the box reopens empty, and the
     * words typed into it are all that is in it.
     */
    @Test
    fun `words typed after a commit replace the old address instead of joining onto it`() {
        val committed = reduceAddressBar(
            state = typed("wikipedia.org"),
            intent = AddressBarIntent.Choose(AddressBarInterpretation.GoTo("wikipedia.org", "wikipedia.org")),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        // The box is gone; the person browses, closes tabs, opens another. When
        // they open the box again it shows the draft that survived all of that,
        // which is what the view model saved and restores.
        val reopened = addressBarShowing(
            input = committed.input,
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        assertEquals("", reopened.input)
        // Nothing to tap that would take them off the page they are reading.
        assertNull(reopened.interpretation)
        assertTrue(reopened.suggestions.isEmpty())

        val asked = reduceAddressBar(
            state = reopened,
            // A keyboard appends: what the field ends up holding is what was
            // already in it followed by what they typed.
            intent = AddressBarIntent.InputChanged(reopened.input + "what is a tapir"),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        assertEquals("what is a tapir", asked.input)
    }

    /**
     * The defect, stated as the thing that used to be dropped.
     *
     * Choosing "Task for Taffy: compare two phones" once opened a screen and
     * handed it nothing at all — no goal, no shape — so the screen had only
     * the task on record to show, which is the *previous* task. A research
     * shape now goes to the Ask sheet, where its pages are named, and it
     * arrives carrying both.
     */
    @Test
    fun `a research shape carries what was typed to the overlay`() {
        val destination = addressBarDestination(
            AddressBarInterpretation.TaskForTaffy(
                "compare two laptops",
                TaskTemplate.SUMMARIZE_EVIDENCE,
            ),
        )

        val sheet = destination as TaffyDestination.AssistantBar
        assertEquals("compare two laptops", sheet.question)
        assertEquals(TaskTemplate.SUMMARIZE_EVIDENCE, sheet.shape)
        // Carried as state, and not as the route it is filed under: the goal is
        // the person's own words, and a route is a key.
        assertFalse(sheet.route.contains("laptops"))
    }

    /** Two readings of two goals go to two screens, not to one screen twice. */
    @Test
    fun `a second research shape opens an overlay of its own`() {
        val first = addressBarDestination(
            AddressBarInterpretation.TaskForTaffy("compare two phones", TaskTemplate.COMPARE_PRODUCTS),
        )
        val second = addressBarDestination(
            AddressBarInterpretation.TaskForTaffy("compare two laptops", TaskTemplate.COMPARE_PRODUCTS),
        )

        assertNotEquals(first.route, second.route)
    }

    /**
     * The same defect one screen along, and the one this test was written for.
     *
     * The resolver offers "Ask Taffy" for any question typed, and choosing it
     * named a bare Assistant bar and dropped the question. Nothing failed and
     * nothing looked empty — SCR-301 opened on whatever the assistant happened
     * to be doing — so the only way to see it is to ask the destination what it
     * is carrying.
     */
    @Test
    fun `the question reading carries what was typed to the Assistant bar`() {
        val destination = addressBarDestination(
            AddressBarInterpretation.AskTaffy("what is a tapir?"),
        )

        val ask = destination as TaffyDestination.AssistantBar
        assertEquals("what is a tapir?", ask.question)
        assertEquals(mapOf(TaffyDestination.QUESTION to "what is a tapir?"), ask.arguments)
        // Carried as state and not as the key it is filed under, for the same
        // reason a goal is.
        assertFalse(ask.route.contains("tapir"))
    }

    /** Two questions are two screens, so the second one is not discarded. */
    @Test
    fun `a second question opens an ask of its own`() {
        val first = addressBarDestination(AddressBarInterpretation.AskTaffy("what is a tapir?"))
        val second = addressBarDestination(AddressBarInterpretation.AskTaffy("what is a tapir?"))

        assertNotEquals(first.route, second.route)
    }

    /** The other two readings go where they always went. */
    @Test
    fun `every other reading lands where it did before`() {
        assertEquals(
            TaffyDestination.BrowserMain,
            addressBarDestination(AddressBarInterpretation.GoTo("docs.example.test", "docs.example.test")),
        )
        assertEquals(
            TaffyDestination.BrowserMain,
            addressBarDestination(AddressBarInterpretation.Search("tapir")),
        )
    }

    // -----------------------------------------------------------------------
    // What the start page's body does with the box's state.
    // -----------------------------------------------------------------------

    /**
     * A caret is not words.
     *
     * The first build folded the welcome away on focus, so tapping the box
     * emptied the page down to a field at the top of the window — the
     * full-screen change decision 0131 removes, wearing the start page's own
     * layout. The rule is one function over the state precisely so that this
     * can be asserted without a window.
     */
    @Test
    fun `an empty box leaves the welcome where it is, and words fold it away`() {
        assertFalse(AddressBarUiState().foldsTheWelcomeAway())
        // A shape stated before anything is typed is still an empty box.
        assertFalse(
            AddressBarUiState(shape = TaskTemplate.COMPARE_PRODUCTS).foldsTheWelcomeAway(),
        )
        // And the plus standing open is not the page being taken over either.
        assertFalse(AddressBarUiState(menuOpen = true).foldsTheWelcomeAway())

        assertTrue(typed("two laptops").foldsTheWelcomeAway())
    }

    /** Leaving gives the welcome back, because leaving empties the box. */
    @Test
    fun `a box that is left stops folding the welcome away`() {
        val left = reduceAddressBar(
            state = typed("two laptops"),
            intent = AddressBarIntent.Left,
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        assertFalse(left.foldsTheWelcomeAway())
    }

    // -----------------------------------------------------------------------
    // The stand-ins for the data layer, which owns resolution and the rows.
    // -----------------------------------------------------------------------

    /** A core that is up and a provider that answers: the start rule says yes. */
    private val ready = StartConditions(
        readiness = TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY),
        availability = CoreUiAvailability.READY,
    )

    private fun errand(input: String): AddressBarInterpretation =
        AddressBarInterpretation.TaskForTaffy(input, TaskTemplate.WEB_ERRAND)

    private fun typed(input: String): AddressBarUiState = reduceAddressBar(
        state = AddressBarUiState(),
        intent = AddressBarIntent.InputChanged(input),
        resolve = ::goTo,
        suggest = ::rowsFor,
    )

    private fun goTo(input: String): AddressBarInterpretation =
        AddressBarInterpretation.GoTo(input, input)

    /** Empty for an empty box, exactly as the repository answers. */
    private fun rowsFor(input: String): List<Suggestion> =
        if (input.isBlank()) emptyList() else listOf(Suggestion("s0", input, goTo(input)))
}
