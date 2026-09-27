// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class BrowserProjectionInvalidatorTest {
    @Test
    fun callbackBurstPublishesOneUnionOfDirtyProjections() {
        val harness = Harness()

        harness.invalidator.tabsChanged()
        harness.invalidator.navigationChanged()
        harness.invalidator.filteringAndNavigationChanged()

        assertEquals(1, harness.queued.size)
        harness.runNext()
        assertEquals(listOf(BrowserProjectionInvalidator.Scope.ALL), harness.published)
    }

    @Test
    fun completedBatchDoesNotSuppressTheNextBrowserTurn() {
        val harness = Harness()
        harness.invalidator.tabsChanged()
        harness.runNext()

        harness.invalidator.navigationChanged()
        harness.runNext()

        assertEquals(2, harness.published.size)
        assertTrue(harness.published[0].tabs)
        assertTrue(harness.published[1].navigation)
    }

    @Test
    fun closeWithdrawsQueuedAndFuturePublications() {
        val harness = Harness()
        harness.invalidator.everythingChanged()

        harness.invalidator.close()
        harness.invalidator.everythingChanged()

        assertTrue(harness.queued.isEmpty())
        assertTrue(harness.published.isEmpty())
    }

    private class Harness {
        val queued = mutableListOf<Runnable>()
        val published = mutableListOf<BrowserProjectionInvalidator.Scope>()
        val invalidator = BrowserProjectionInvalidator(
            schedule = queued::add,
            cancel = queued::remove,
            publish = published::add,
        )

        fun runNext() {
            queued.removeFirst().run()
        }
    }
}
