// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.ProviderRoute
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-004's primary action, and what its label promises. */
class AiSetupFooterActionTest {

    @Test
    fun `nothing chosen keeps the primary action disabled`() {
        val state = AiSetupUiState()
        assertEquals(AiSetupFooterAction.CONTINUE, aiSetupFooterAction(state))
        assertFalse(state.hasChosen)
    }

    @Test
    fun `task only no model route never appears as a configured provider`() {
        val state = AiSetupUiState(route = ProviderRoute.NO_MODEL_REQUIRED)
        assertEquals(AiSetupFooterAction.CONTINUE, aiSetupFooterAction(state))
        assertFalse(state.hasChosen)
    }

    @Test
    fun `the one route enters provider setup`() {
        val state = AiSetupUiState(route = ProviderRoute.DIRECT_WITH_YOUR_KEY)
        assertEquals(AiSetupFooterAction.CHOOSE_PROVIDER, aiSetupFooterAction(state))
        assertTrue(state.hasChosen)
    }

    @Test
    fun `provider setup is not labelled as a generic continuation`() {
        assertNotEquals(
            aiSetupFooterAction(AiSetupUiState()),
            aiSetupFooterAction(
                AiSetupUiState(route = ProviderRoute.DIRECT_WITH_YOUR_KEY),
            ),
        )
    }
}
