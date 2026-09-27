// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HelpReducerTest {

    @Test
    fun `a draft no email app took makes the screen name the address`() {
        val after = reduceHelp(HelpUiState(), HelpIntent.FeedbackByEmail(opened = false))

        assertTrue(after.emailUnavailable)
    }

    @Test
    fun `a draft an email app took clears the note`() {
        val after = reduceHelp(
            HelpUiState(emailUnavailable = true),
            HelpIntent.FeedbackByEmail(opened = true),
        )

        assertFalse(after.emailUnavailable)
    }

    @Test
    fun `opening an issue and leaving change nothing the screen shows`() {
        val state = HelpUiState(emailUnavailable = true)

        assertEquals(state, reduceHelp(state, HelpIntent.OpenPublicIssue))
        assertEquals(state, reduceHelp(state, HelpIntent.Dismiss))
    }
}
