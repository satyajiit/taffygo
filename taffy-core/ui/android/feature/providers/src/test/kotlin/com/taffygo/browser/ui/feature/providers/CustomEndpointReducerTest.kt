// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-418's screen-local fold, driven without a core.
 *
 * The rule under test throughout is that a statement about one address never
 * outlives that address: a probe answer, a proposal and a kept-as-typed answer
 * are all about one exact string, and anything that changes the string throws
 * all three away.
 */
class CustomEndpointReducerTest {

    private val probed = CustomEndpointDraft(
        address = "http://192.168.1.9:11434",
        addressTouched = true,
        outcome = CustomEndpointOutcome.Reached(
            server = CustomEndpointOutcome.ServerKind.OLLAMA,
            modelCount = 4,
            models = listOf("a", "b", "c", "d").map(::fakeModel),
            provedBase = "http://192.168.1.9:11434/v1",
        ),
        proposal = "http://192.168.1.9:11434/v1",
    )

    @Test
    fun `typing throws away everything said about the old address`() {
        val next = CustomEndpointReducer.reduce(
            probed.copy(keptAsTyped = true, saveRefused = true),
            CustomEndpointIntent.ChangeAddress("http://192.168.1.9:114"),
        )

        assertEquals("http://192.168.1.9:114", next.address)
        assertNull(next.outcome)
        assertNull(next.proposal)
        assertFalse(next.keptAsTyped)
        assertFalse(next.saveRefused)
    }

    /**
     * The one field that deliberately does **not** follow the address rule.
     *
     * A check that answered "not without a key" is the sentence telling a
     * person to type this, and clearing it at the first character would take
     * the instruction away while they act on it. What does go is the store's
     * refusal, which was about the key before this one.
     */
    @Test
    fun `typing a key keeps the answer that asked for one`() {
        val wantsAKey = probed.copy(
            outcome = CustomEndpointOutcome.Refused(
                CustomEndpointOutcome.Problem.WANTS_A_CREDENTIAL,
            ),
            proposal = null,
            keySealRefused = true,
        )

        val next = CustomEndpointReducer.reduce(
            wantsAKey,
            CustomEndpointIntent.ChangeKey("sk-local-not-a-real-key"),
        )

        assertEquals("sk-local-not-a-real-key", next.key)
        assertEquals(wantsAKey.outcome, next.outcome)
        assertFalse(next.keySealRefused)
    }

    @Test
    fun `the advanced part and the key's own dots are a person's to open`() {
        val opened = CustomEndpointReducer.reduce(probed, CustomEndpointIntent.ToggleAdvanced)
        val revealed =
            CustomEndpointReducer.reduce(opened, CustomEndpointIntent.ToggleKeyVisible)

        assertTrue(opened.advancedOpen)
        assertTrue(revealed.keyRevealed)
        assertFalse(
            CustomEndpointReducer.reduce(opened, CustomEndpointIntent.ToggleAdvanced).advancedOpen,
        )
    }

    @Test
    fun `a preset is typing, and clears the same things`() {
        val next = CustomEndpointReducer.reduce(
            probed,
            CustomEndpointIntent.UsePreset(CustomEndpointAddress.Preset.LM_STUDIO),
        )

        assertEquals(CustomEndpointAddress.Preset.LM_STUDIO.address, next.address)
        assertNull(next.outcome)
        assertNull(next.proposal)
    }

    @Test
    fun `accepting the proposal makes it the address and leaves nothing to propose`() {
        val next = CustomEndpointReducer.reduce(probed, CustomEndpointIntent.AcceptProposal)

        assertEquals("http://192.168.1.9:11434/v1", next.address)
        assertNull(next.proposal)
        assertFalse(next.keptAsTyped)
        // The check is about the same server, reached through the path that
        // server publishes, so its answer survives the correction.
        assertEquals(probed.outcome, next.outcome)
    }

    @Test
    fun `keeping the address answers the question and leaves the caution standing`() {
        val next = CustomEndpointReducer.reduce(probed, CustomEndpointIntent.KeepAddress)

        assertEquals("http://192.168.1.9:11434", next.address)
        assertEquals("http://192.168.1.9:11434/v1", next.proposal)
        assertTrue(next.keptAsTyped)
    }

    @Test
    fun `keeping does nothing when nothing was proposed`() {
        val draft = CustomEndpointDraft(address = "http://192.168.1.9:11434/v1")

        assertEquals(draft, CustomEndpointReducer.reduce(draft, CustomEndpointIntent.KeepAddress))
    }

    @Test
    fun `nothing moves under a write that is already out`() {
        val writing = probed.copy(saving = true)

        assertEquals(
            writing,
            CustomEndpointReducer.reduce(writing, CustomEndpointIntent.ChangeAddress("x")),
        )
        assertEquals(
            writing,
            CustomEndpointReducer.reduce(writing, CustomEndpointIntent.ChangeName("x")),
        )
    }

    @Test
    fun `a check that is still out does not freeze the address`() {
        val checking = probed.copy(probing = true)

        val next = CustomEndpointReducer.reduce(
            checking,
            CustomEndpointIntent.ChangeAddress("http://192.168.1.9:11435"),
        )

        // A check changes nothing durable and can take the best part of a
        // minute. What keeps this safe is the view model dropping an answer
        // about an address the field no longer holds.
        assertEquals("http://192.168.1.9:11435", next.address)
        assertNull(next.proposal)
    }

    @Test
    fun `the name field remembers that it was touched`() {
        val next = CustomEndpointReducer.reduce(
            CustomEndpointDraft(),
            CustomEndpointIntent.ChangeName(""),
        )

        assertTrue(next.nameTouched)
    }

    @Test
    fun `the confirmation never reopens over a removal already sent`() {
        val running = CustomEndpointDraft(deleting = true)

        assertFalse(
            CustomEndpointReducer.reduce(running, CustomEndpointIntent.AskDelete).confirmingDelete,
        )
        assertTrue(
            CustomEndpointReducer.reduce(CustomEndpointDraft(), CustomEndpointIntent.AskDelete)
                .confirmingDelete,
        )
    }
}
