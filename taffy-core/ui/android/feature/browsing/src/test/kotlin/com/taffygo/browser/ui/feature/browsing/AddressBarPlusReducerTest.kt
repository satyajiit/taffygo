// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.ui.TaffyDestination
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The plus on the box: a shape stated on the question and a store attached to
 * the request, and the chips that say so. The reducer's other half is
 * [AddressBarReducerTest]; the stand-ins at the bottom are the same ones.
 */
class AddressBarPlusReducerTest {

    // -----------------------------------------------------------------------
    // The plus: a store attached to the request, and the chip that says so.
    // -----------------------------------------------------------------------

    /** Attaching answers the menu, so the menu closes with it; the same tap again takes it off. */
    @Test
    fun `a store attached from the plus closes the menu and rides on the start`() {
        val asked = reduceAddressBar(
            state = AddressBarUiState(conditions = ready),
            intent = AddressBarIntent.InputChanged("download my aadhaar"),
            resolve = ::errand,
            suggest = ::rowsFor,
        )
        val opened = reduceAddressBar(
            state = asked,
            intent = AddressBarIntent.OpenMenu,
            resolve = ::errand,
            suggest = ::rowsFor,
        )
        val attached = reduceAddressBar(
            state = opened,
            intent = AddressBarIntent.ToggleStore(TaskAttachedStore.HISTORY),
            resolve = ::errand,
            suggest = ::rowsFor,
        )

        assertFalse(attached.menuOpen)
        assertEquals(setOf(TaskAttachedStore.HISTORY), attached.attachedStores)
        // The words and their reading are untouched; what changed is what the
        // start rule hands Taffy.
        assertEquals("download my aadhaar", attached.input)
        val start = attached.start as TaskStartDecision.Start
        assertEquals(setOf(TaskAttachedStore.HISTORY), start.request.consent.attachedStores)

        val both = reduceAddressBar(
            state = attached,
            intent = AddressBarIntent.ToggleStore(TaskAttachedStore.OPEN_TABS),
            resolve = ::errand,
            suggest = ::rowsFor,
        )
        assertEquals(setOf(TaskAttachedStore.HISTORY, TaskAttachedStore.OPEN_TABS), both.attachedStores)

        val detached = reduceAddressBar(
            state = both,
            intent = AddressBarIntent.ToggleStore(TaskAttachedStore.HISTORY),
            resolve = ::errand,
            suggest = ::rowsFor,
        )
        assertEquals(setOf(TaskAttachedStore.OPEN_TABS), detached.attachedStores)
    }

    /** A store set before typing survives the typing, and leaves with the words. */
    @Test
    fun `typing keeps an attached store and leaving drops it`() {
        val attached = reduceAddressBar(
            state = AddressBarUiState(),
            intent = AddressBarIntent.ToggleStore(TaskAttachedStore.BOOKMARKS),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )
        val typed = reduceAddressBar(
            state = attached,
            intent = AddressBarIntent.InputChanged("two laptops"),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )
        assertEquals(setOf(TaskAttachedStore.BOOKMARKS), typed.attachedStores)

        for (leaving in listOf(AddressBarIntent.Dismiss, AddressBarIntent.Left)) {
            val left = reduceAddressBar(typed, leaving, ::goTo, ::rowsFor)
            assertEquals(leaving.toString(), emptySet<TaskAttachedStore>(), left.attachedStores)
        }
    }

    // -----------------------------------------------------------------------
    // The plus: a shape stated on the question, and the chip that says so.
    // -----------------------------------------------------------------------

    @Test
    fun `a stated shape is what the box means, over what the words resolved to`() {
        val typed = typed("two laptops")
        val stated = reduceAddressBar(
            state = typed,
            intent = AddressBarIntent.ChooseShape(TaskTemplate.COMPARE_PRODUCTS),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        // The resolver still says what it said — nothing is overwritten — and
        // the reading is the person's own answer laid over it.
        assertTrue(stated.interpretation is AddressBarInterpretation.GoTo)
        val reading = stated.reading
        assertTrue(reading is AddressBarInterpretation.TaskForTaffy)
        assertEquals(
            TaskTemplate.COMPARE_PRODUCTS,
            (reading as AddressBarInterpretation.TaskForTaffy).template,
        )
        assertEquals("two laptops", reading.input)
        // A research shape is not a start from here: it goes to the sheet.
        assertNull(stated.start)
    }

    /** A chip with no words behind it is a chip waiting, not a task. */
    @Test
    fun `a shape on an empty box means nothing yet`() {
        val stated = reduceAddressBar(
            state = AddressBarUiState(),
            intent = AddressBarIntent.ChooseShape(TaskTemplate.WEB_ERRAND),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        assertNull(stated.reading)
        assertNull(stated.start)
    }

    /** Stating one answers the menu, so the menu closes with it. */
    @Test
    fun `the menu opens, states a shape, and is closed by the stating`() {
        val opened = reduceAddressBar(
            state = AddressBarUiState(),
            intent = AddressBarIntent.OpenMenu,
            resolve = ::goTo,
            suggest = ::rowsFor,
        )
        assertTrue(opened.menuOpen)

        val stated = reduceAddressBar(
            state = opened,
            intent = AddressBarIntent.ChooseShape(TaskTemplate.SUMMARIZE_EVIDENCE),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        assertFalse(stated.menuOpen)
        assertEquals(TaskTemplate.SUMMARIZE_EVIDENCE, stated.shape)
    }

    /**
     * A chip set before typing survives the typing.
     *
     * The projection of new text holds nothing but the text, so without the
     * carry-over the chip came off on the first keystroke — and again on every
     * one after it.
     */
    @Test
    fun `typing after stating a shape keeps the shape`() {
        val stated = reduceAddressBar(
            state = AddressBarUiState(),
            intent = AddressBarIntent.ChooseShape(TaskTemplate.COMPARE_PRODUCTS),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )
        val typed = reduceAddressBar(
            state = stated,
            intent = AddressBarIntent.InputChanged("two laptops"),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        assertEquals(TaskTemplate.COMPARE_PRODUCTS, typed.shape)
    }

    @Test
    fun `taking the chip off gives the words back to the resolver`() {
        val stated = reduceAddressBar(
            state = typed("two laptops"),
            intent = AddressBarIntent.ChooseShape(TaskTemplate.COMPARE_PRODUCTS),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )
        val cleared = reduceAddressBar(
            state = stated,
            intent = AddressBarIntent.ClearShape,
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        assertNull(cleared.shape)
        assertTrue(cleared.reading is AddressBarInterpretation.GoTo)
    }

    /**
     * Leaving the box empties it, chip included.
     *
     * [AddressBarIntent.Left] is the start page's own way of saying the surface
     * has gone — screen SCR-101 is the bottom of the back stack and its entry is
     * never released, so without this the draft would wait in the next empty
     * tab's box. It is reduced exactly as giving up is, because it is the same
     * event with no screen to leave.
     */
    @Test
    fun `a box that is left keeps neither the words nor the chip`() {
        val stated = reduceAddressBar(
            state = typed("two laptops"),
            intent = AddressBarIntent.ChooseShape(TaskTemplate.COMPARE_PRODUCTS),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        for (leaving in listOf(AddressBarIntent.Left, AddressBarIntent.Dismiss)) {
            val left = reduceAddressBar(
                state = stated,
                intent = leaving,
                resolve = ::goTo,
                suggest = ::rowsFor,
            )

            assertEquals(leaving.toString(), "", left.input)
            assertNull(leaving.toString(), left.shape)
            assertNull(leaving.toString(), left.reading)
        }
    }

    /** A chip is carried to the sheet, not dropped at the door. */
    @Test
    fun `a stated shape reaches the sheet on the destination`() {
        val stated = reduceAddressBar(
            state = typed("two laptops"),
            intent = AddressBarIntent.ChooseShape(TaskTemplate.BUILD_A_SOURCE_TABLE),
            resolve = ::goTo,
            suggest = ::rowsFor,
        )

        val destination = addressBarDestination(checkNotNull(stated.reading))

        // The arguments and not the route: an ask takes an identity of its
        // own so two of them are two screens, which is exactly what makes the
        // route the wrong thing to compare.
        assertEquals(TaffyDestination.AssistantBar.SCREEN_ID, destination.screenId)
        assertEquals(
            TaffyDestination.AssistantBar(
                question = "two laptops",
                shape = TaskTemplate.BUILD_A_SOURCE_TABLE,
            ).arguments,
            destination.arguments,
        )
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
