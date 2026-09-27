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

/** The four things a person can do on screen SCR-415 that reach nothing. */
class ProviderConfigReducerTest {

    @Test
    fun `typing clears the last refusal and the last success together`() {
        val after = ProviderConfigReducer.reduce(
            ProviderConfigDraft(
                key = "old",
                problem = ProviderKeyProblem.KEY_REFUSED,
                stored = true,
            ),
            ProviderConfigIntent.ChangeKey("new"),
        )

        assertEquals("new", after.key)
        assertNull(after.problem)
        assertFalse(after.stored)
    }

    @Test
    fun `typing while a call is out changes nothing, because that draft is the one flying`() {
        val flying = ProviderConfigDraft(key = "proving", probing = true)

        val after = ProviderConfigReducer.reduce(flying, ProviderConfigIntent.ChangeKey("other"))

        assertEquals(flying, after)
    }

    @Test
    fun `unmasking toggles and touches nothing else`() {
        val start = ProviderConfigDraft(key = "abc", problem = ProviderKeyProblem.EMPTY)

        val shown = ProviderConfigReducer.reduce(start, ProviderConfigIntent.ToggleKeyVisible)
        val hidden = ProviderConfigReducer.reduce(shown, ProviderConfigIntent.ToggleKeyVisible)

        assertTrue(shown.revealed)
        assertFalse(hidden.revealed)
        assertEquals(ProviderKeyProblem.EMPTY, hidden.problem)
    }

    @Test
    fun `the removal confirmation opens and closes`() {
        val asked = ProviderConfigReducer.reduce(
            ProviderConfigDraft(),
            ProviderConfigIntent.AskSignOut,
        )
        val closed = ProviderConfigReducer.reduce(asked, ProviderConfigIntent.CancelSignOut)

        assertTrue(asked.confirmingSignOut)
        assertFalse(closed.confirmingSignOut)
    }

    @Test
    fun `a removal already running cannot be asked for a second time`() {
        val running = ProviderConfigDraft(signingOut = true)

        val after = ProviderConfigReducer.reduce(running, ProviderConfigIntent.AskSignOut)

        assertFalse(after.confirmingSignOut)
    }

    @Test
    fun `an intent that reaches the browser is left entirely to the view model`() {
        val start = ProviderConfigDraft(key = "abc")

        val consequential = listOf(
            ProviderConfigIntent.SaveKey,
            ProviderConfigIntent.SaveKeyAnyway,
            ProviderConfigIntent.ConfirmSignOut,
            ProviderConfigIntent.UseForTaffy,
            ProviderConfigIntent.StartSignIn,
            ProviderConfigIntent.ChangeModel,
            ProviderConfigIntent.OpenKeyPage,
            ProviderConfigIntent.OpenDocs,
        )

        consequential.forEach { assertEquals(start, ProviderConfigReducer.reduce(start, it)) }
    }
}
