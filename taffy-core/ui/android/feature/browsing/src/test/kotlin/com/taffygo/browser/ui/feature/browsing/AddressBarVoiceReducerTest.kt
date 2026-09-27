// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.AddressBarCommand
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.VoiceEntryState
import com.taffygo.browser.ui.core.ui.VoiceTranscript
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class AddressBarVoiceReducerTest {
    @Test
    fun `review leaves the address and its reading untouched until confirmation`() {
        val initial = addressBarShowing(
            input = "typed.example",
            resolve = ::goTo,
            suggest = { emptyList() },
        ).copy(voiceEntry = VoiceEntryState.Review(transcript("spoken search")))

        assertEquals("typed.example", initial.input)

        val confirmed = reduceAddressBar(
            state = initial,
            intent = AddressBarIntent.ConfirmVoiceInput,
            resolve = { AddressBarInterpretation.Search(it) },
            suggest = { emptyList() },
        )

        assertEquals("spoken search", confirmed.input)
        assertEquals(AddressBarInterpretation.Search("spoken search"), confirmed.interpretation)
        assertEquals(VoiceEntryState.Closed, confirmed.voiceEntry)
    }

    @Test
    fun `cancel discards transient speech without changing the draft`() {
        val initial = AddressBarUiState(
            input = "typed.example",
            interpretation = goTo("typed.example"),
            voiceEntry = VoiceEntryState.Review(transcript("secret words")),
        )

        val cancelled = reduceAddressBar(
            initial,
            AddressBarIntent.CancelVoiceInput,
            ::goTo,
            { emptyList() },
        )

        assertEquals("typed.example", cancelled.input)
        assertEquals(VoiceEntryState.Closed, cancelled.voiceEntry)
    }

    @Test
    fun `every safe browser command maps to its reviewed destination`() {
        val destinations = AddressBarCommand.entries.associateWith { command ->
            addressBarDestination(AddressBarInterpretation.BrowserCommand("command", command))
        }

        assertEquals(TaffyDestination.History, destinations[AddressBarCommand.OPEN_HISTORY])
        assertEquals(TaffyDestination.Bookmarks, destinations[AddressBarCommand.OPEN_BOOKMARKS])
        assertEquals(TaffyDestination.Downloads, destinations[AddressBarCommand.OPEN_DOWNLOADS])
        assertEquals(TaffyDestination.SettingsHome, destinations[AddressBarCommand.OPEN_SETTINGS])
        assertEquals(
            TaffyDestination.ClearBrowsingData,
            destinations[AddressBarCommand.OPEN_CLEAR_BROWSING_DATA],
        )
    }

    @Test
    fun `an unconfirmed transcript has no reading`() {
        val started = reduceAddressBar(
            AddressBarUiState(),
            AddressBarIntent.StartVoiceInput,
            ::goTo,
            { emptyList() },
        )

        assertEquals(VoiceEntryState.RequestingPermission, started.voiceEntry)
        assertNull(started.interpretation)
    }

    private fun transcript(text: String): VoiceTranscript =
        requireNotNull(VoiceTranscript.bounded(text))

    private fun goTo(input: String): AddressBarInterpretation =
        AddressBarInterpretation.GoTo(input, input)
}
