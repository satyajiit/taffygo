// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

/** Screen SCR-404 folds one intent and navigates on the rest. */
class ProviderHubReducerTest {

    @Test
    fun `choosing a category is the whole of what the tabs change`() {
        val state = hubState(ProviderHubGroup.BRING_YOUR_OWN_KEY)

        val next = ProviderHubReducer.reduce(
            state,
            ProviderHubIntent.ShowCategory(ProviderHubGroup.SUBSCRIPTION),
        )

        assertEquals(ProviderHubGroup.SUBSCRIPTION, next.showing)
        // Nothing else moves. The rows are the roster's and the reducer has no
        // business rewriting them.
        assertEquals(state.sections, next.sections)
        assertEquals(state.status, next.status)
    }

    @Test
    fun `a category that holds nothing is still a category a person may choose`() {
        val state = hubState(ProviderHubGroup.BRING_YOUR_OWN_KEY)

        val next = ProviderHubReducer.reduce(
            state,
            ProviderHubIntent.ShowCategory(ProviderHubGroup.YOUR_OWN_ENDPOINT),
        )

        // Refusing the press would leave a tab that looks pressable and is not,
        // with no way to find out why it holds nothing. The screen says so
        // instead.
        assertEquals(ProviderHubGroup.YOUR_OWN_ENDPOINT, next.showing)
        assertEquals(0, next.countIn(next.showing))
        assertEquals(emptyList<ProviderHubRow>(), next.rowsIn(next.showing))
    }

    @Test
    fun `every tab is reachable from every other`() {
        ProviderHubGroup.entries.forEach { from ->
            ProviderHubGroup.entries.forEach { to ->
                val next = ProviderHubReducer.reduce(
                    hubState(from),
                    ProviderHubIntent.ShowCategory(to),
                )
                assertEquals(to, next.showing)
            }
        }
    }

    @Test
    fun `the two navigating intents leave the screen exactly as it was`() {
        val state = hubState(ProviderHubGroup.BRING_YOUR_OWN_KEY)
        val row = state.rows.single()

        assertSame(state, ProviderHubReducer.reduce(state, ProviderHubIntent.OpenRow(row)))
        assertSame(state, ProviderHubReducer.reduce(state, ProviderHubIntent.AddYourOwnProvider))
    }

    /** One provider taking a key, which is the smallest state with a tab row. */
    private fun hubState(showing: ProviderHubGroup): ProviderHubUiState = ProviderHubUiState(
        status = ProviderHubStatus.READY,
        sections = listOf(
            ProviderHubSection(
                group = ProviderHubGroup.BRING_YOUR_OWN_KEY,
                rows = listOf(
                    ProviderHubRow(
                        providerId = "keyed",
                        displayName = "Keyed",
                        group = ProviderHubGroup.BRING_YOUR_OWN_KEY,
                        offer = ProviderRowOffer.Configure,
                        wayIn = ProviderWayIn.KEY,
                        signingIn = false,
                    ),
                ),
            ),
        ),
        showing = showing,
    )
}
