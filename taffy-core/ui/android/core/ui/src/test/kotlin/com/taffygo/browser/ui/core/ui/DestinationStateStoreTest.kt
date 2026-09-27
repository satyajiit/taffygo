// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.lifecycle.HasDefaultViewModelProviderFactory
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.CreationExtras
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.ui.internal.DestinationStateStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A screen's view models and saved state, for exactly as long as the back stack
 * holds the screen — and keyed by nothing the person typed.
 *
 * Both halves are asserted here because both were wrong together, and each one
 * made the other worse. Every visit to a destination with arguments left a
 * saved-state handle behind that nothing ever removed, so the map grew for as
 * long as the window lived; and the since-retired task ask's key was the
 * goal in the person's own words, so what grew the map was what they had
 * typed, and it stayed under its own name.
 *
 * These are host tests. The store's keys and lifetimes are ordinary Kotlin, and
 * the entry's saved state is the platform's own — created, read and cleared
 * here exactly as a window would, with no window, no device and no frame.
 */
class DestinationStateStoreTest {

    private val host = NoWindow()

    /**
     * The growth, stated as arithmetic.
     *
     * Fifty asks opened and left is fifty routes, fifty keys and fifty
     * goals under the old rule. What the store holds afterwards is the one
     * screen the person is actually on.
     */
    @Test
    fun `a ask opened and left again and again leaves nothing behind`() {
        val store = DestinationStateStore(LifecycleRegistry::createUnsafe)
        var stack = BackStack.start(TaffyDestination.BrowserMain)
        store.entryFor(host, TaffyDestination.BrowserMain)

        repeat(TIMES_OPENED) { visit ->
            val ask = TaffyDestination.AssistantBar("compare the $visit models I looked at")
            val opened = stack.push(ask)
            store.entryFor(host, ask)
            stack = opened.pop()
            store.release(stack.popped(opened).map { it.route })
        }

        assertEquals(setOf(TaffyDestination.BrowserMain.route), store.routes)
    }

    /**
     * What a pop releases is the state itself, not merely an entry in a map.
     *
     * The second half is the part that would fail quietly: a store that dropped
     * the key but kept the entry would leave the person's words in memory and
     * would open the next ask holding them.
     */
    @Test
    fun `a destination the stack has left behind loses what was typed into it`() {
        val store = DestinationStateStore(LifecycleRegistry::createUnsafe)
        val ask = TaffyDestination.AssistantBar("compare two phones")
        val opened = BackStack.start().push(ask)
        val entry = store.entryFor(host, ask)
        entry.savedStateFor(ask)[TaffyDestination.QUESTION] = "compare two laptops"

        val left = opened.pop()
        store.release(left.popped(opened).map { it.route })

        assertFalse(store.routes.contains(ask.route))
        val reopened = store.entryFor(host, ask)
        assertNotSame(entry, reopened)
        assertEquals(
            "compare two phones",
            reopened.savedStateFor(ask).get<String>(TaffyDestination.QUESTION),
        )
    }

    /**
     * The other direction, which is what stops the fix from being a fault of a
     * different kind: a screen navigated away from and returned to still holds
     * what was typed into it, because it never left the back stack.
     */
    @Test
    fun `a destination still on the stack keeps what was typed into it`() {
        val store = DestinationStateStore(LifecycleRegistry::createUnsafe)
        val ask = TaffyDestination.AssistantBar("compare two phones")
        val opened = BackStack.start().push(ask)
        store.entryFor(host, ask).savedStateFor(ask)[TaffyDestination.QUESTION] =
            "compare two laptops"

        val onward = opened.push(TaffyDestination.TaskView)
        store.release(onward.popped(opened).map { it.route })

        assertTrue(store.routes.contains(ask.route))
        val returned = store.entryFor(host, ask)
        assertEquals(
            "compare two laptops",
            returned.savedStateFor(ask).get<String>(TaffyDestination.QUESTION),
        )
    }

    /**
     * The privacy half: a key names the screen, and the goal is state the
     * screen reads.
     *
     * The goal is asserted to be *present* as well, because a store that had
     * simply lost it would pass the first assertion and fail the person.
     */
    @Test
    fun `no key the store holds carries what the person typed`() {
        val store = DestinationStateStore(LifecycleRegistry::createUnsafe)
        val goal = "book a table at the wine bar for four on tuesday"
        val ask = TaffyDestination.AssistantBar(goal, shape = TaskTemplate.SUMMARIZE_EVIDENCE)

        val handle = store.entryFor(host, ask).savedStateFor(ask)

        assertEquals(
            setOf("${TaffyDestination.AssistantBar.SCREEN_ID}/${ask.askId}"),
            store.routes,
        )
        assertTrue(store.routes.none { key -> key.contains(goal) || key.contains("wine") })
        assertEquals(goal, handle.get<String>(TaffyDestination.QUESTION))
        assertEquals(
            TaskTemplate.SUMMARIZE_EVIDENCE.label,
            handle.get<String>(TaffyDestination.SHAPE),
        )
    }

    /**
     * Two asks about the same words are two screens.
     *
     * This is what the goal-shaped key was doing correctly and is why it could
     * not simply be deleted: a shared key meant the second ask opened
     * holding the first one's edits. An identity minted per ask keeps them
     * apart without keying on anything the person wrote.
     */
    @Test
    fun `two asks about the same goal do not share state`() {
        val store = DestinationStateStore(LifecycleRegistry::createUnsafe)
        val first = TaffyDestination.AssistantBar("compare two phones")
        val second = TaffyDestination.AssistantBar("compare two phones")

        store.entryFor(host, first).savedStateFor(first)[TaffyDestination.QUESTION] =
            "compare two laptops"
        val untouched = store.entryFor(host, second).savedStateFor(second)

        assertEquals(2, store.routes.size)
        assertEquals("compare two phones", untouched.get<String>(TaffyDestination.QUESTION))
    }

    /**
     * A screen that is leaving is still a screen, until it has left.
     *
     * A navigation transition keeps the outgoing screen composed for a couple
     * of hundred milliseconds after the back stack has moved on, and a
     * recomposition in that window asks for the view model again. An entry
     * ended the instant it was popped answers that by building a second view
     * model, on a scope nothing will ever clear — so the release waits for the
     * screen, and the screen says when it is gone.
     */
    @Test
    fun `a released entry ends when its screen is gone rather than when it is popped`() {
        val store = DestinationStateStore(LifecycleRegistry::createUnsafe)
        val ask = TaffyDestination.AssistantBar("compare two phones")
        val entry = store.entryFor(host, ask)
        entry.onComposed()
        entry.savedStateFor(ask)[TaffyDestination.QUESTION] = "compare two laptops"

        store.release(listOf(ask.route))

        assertEquals(
            "compare two laptops",
            entry.savedStateFor(ask).get<String>(TaffyDestination.QUESTION),
        )

        entry.onDisposed()

        assertEquals(
            "compare two phones",
            entry.savedStateFor(ask).get<String>(TaffyDestination.QUESTION),
        )
    }

    /** One destination, one entry, however often it is drawn. */
    @Test
    fun `a destination asked for twice is answered with the same entry`() {
        val store = DestinationStateStore(LifecycleRegistry::createUnsafe)
        val ask = TaffyDestination.AssistantBar("compare two phones")

        assertSame(store.entryFor(host, ask), store.entryFor(host, ask))
        assertEquals(1, store.routes.size)
    }

    /**
     * A window with nothing behind it, which is all an entry takes from one:
     * the factory its screens are built by, which no test here builds a screen
     * through.
     */
    private class NoWindow : HasDefaultViewModelProviderFactory {
        override val defaultViewModelProviderFactory: ViewModelProvider.Factory =
            object : ViewModelProvider.Factory {}

        override val defaultViewModelCreationExtras: CreationExtras = CreationExtras.Empty
    }

    private companion object {
        /** Enough visits that growth of one entry per visit would be obvious. */
        const val TIMES_OPENED = 50
    }
}
