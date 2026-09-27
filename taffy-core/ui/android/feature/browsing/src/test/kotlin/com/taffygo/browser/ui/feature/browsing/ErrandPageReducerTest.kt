// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.ErrandPage
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.model.PageLoadFailure
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** What screen SCR-110 says about the page an errand is on. */
class ErrandPageReducerTest {

    @Test
    fun `the origin, the lock and the badge come from the navigation`() {
        val state = ErrandPageReducer.project(
            ErrandPage(
                errandId = ERRAND,
                navigation = NavigationState(
                    host = "accounts.google.com",
                    title = "Sign in",
                    canGoBack = true,
                    isLoading = true,
                    isSecure = true,
                    blockedRequestCount = 3,
                ),
            ),
            ERRAND,
        )

        assertEquals("accounts.google.com", state.host)
        assertTrue(state.isSecure)
        assertTrue(state.isLoading)
        assertTrue(state.canGoBack)
        assertEquals(3, state.blockedCount)
        assertFalse(state.gone)
    }

    /**
     * A certificate that did not check out is not a lock, and this screen is
     * the last place that could afford to draw one anyway: it is where a person
     * decides whether to hand over a credential.
     */
    @Test
    fun `a failure is not a private connection`() {
        val state = ErrandPageReducer.project(
            ErrandPage(
                errandId = ERRAND,
                navigation = NavigationState(
                    host = "vendor.test",
                    title = "",
                    isSecure = false,
                    failure = PageLoadFailure.CERTIFICATE_INVALID,
                ),
            ),
            ERRAND,
        )

        assertFalse(state.isSecure)
    }

    @Test
    fun `no page at all is gone`() {
        assertTrue(ErrandPageReducer.project(null, ERRAND).gone)
    }

    /**
     * One errand runs at a time, so a screen still composed for a previous one
     * must not adopt its successor. Without the identity check it would draw
     * somebody else's page under its own toolbar and offer a back button that
     * walked it.
     */
    @Test
    fun `another errand's page is gone rather than borrowed`() {
        val state = ErrandPageReducer.project(
            ErrandPage(
                errandId = "errand-2",
                navigation = NavigationState(host = "elsewhere.test", title = ""),
            ),
            ERRAND,
        )

        assertTrue(state.gone)
        assertEquals("", state.host)
    }

    private companion object {
        const val ERRAND = "errand-1"
    }
}
