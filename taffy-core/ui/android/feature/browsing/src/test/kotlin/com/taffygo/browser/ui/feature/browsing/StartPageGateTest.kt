// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartPurpose
import com.taffygo.browser.ui.core.model.TaffyPartsState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The start page waits on the Python library, and on nothing else. */
class StartPageGateTest {

    @Test
    fun `a build that publishes a library but has not listed it is not ready`() {
        val gate = startPageGate(TaffyPartsState(answered = true, supported = true))

        assertFalse(gate.ready)
        assertNull(gate.partId)
    }

    @Test
    fun `a browser that has said nothing is not ready and is always retryable`() {
        val gate = startPageGate(TaffyPartsState())

        assertFalse(gate.ready)
        assertFalse(gate.answered)
        assertNull(gate.partId)
        // The stall this replaced: with no snapshot there is no row to ask
        // about, so the screen offered nothing and waited for the life of
        // the process. The core itself is what is left to ask.
        assertTrue(gate.canRetry)
    }

    @Test
    fun `a build with nothing published for this device opens the start page`() {
        val gate = startPageGate(TaffyPartsState(answered = true, supported = false))

        assertTrue(gate.ready)
        assertNull(gate.partId)
    }

    @Test
    fun `an installed library opens the start page`() {
        val gate = startPageGate(parts(availability = TaffyPartAvailability.READY))

        assertTrue(gate.ready)
        assertEquals(TaffyPartId("python-stdlib"), gate.partId)
    }

    @Test
    fun `a library that is still arriving keeps the start page closed`() {
        val missing = startPageGate(parts(availability = TaffyPartAvailability.MISSING))
        val partial = startPageGate(parts(availability = TaffyPartAvailability.PARTIAL))
        val checking = startPageGate(parts(availability = TaffyPartAvailability.CHECKING))

        assertFalse(missing.ready)
        assertFalse(partial.ready)
        assertFalse(checking.ready)
    }

    @Test
    fun `country flags do not hold the start page`() {
        val flags = TaffyPart(
            id = TaffyPartId("country-flags"),
            version = "7.5.0-taffy.1",
            purpose = TaffyPartPurpose.COUNTRY_FLAGS,
            availability = TaffyPartAvailability.MISSING,
            downloadedBytes = 0,
            totalBytes = 584_892,
            attempts = 0,
            hold = null,
        )
        val gate = startPageGate(
            TaffyPartsState(
                answered = true,
                supported = true,
                parts = listOf(
                    library(availability = TaffyPartAvailability.READY),
                    flags,
                ),
            ),
        )

        assertTrue(gate.ready)
    }

    @Test
    fun `live progress moves the bar forward and never back`() {
        val snapshot = parts(
            availability = TaffyPartAvailability.PARTIAL,
            downloadedBytes = 100,
            totalBytes = 1_000,
        )
        val live = TaffyPartProgress(
            id = TaffyPartId("python-stdlib"),
            version = "3.14.7-taffy.1",
            downloadedBytes = 400,
            totalBytes = 1_000,
        )

        val gate = startPageGate(snapshot, mapOf(live.id to live))

        assertEquals(400L, gate.downloadedBytes)
        assertEquals(0.4f, gate.fraction)
    }

    @Test
    fun `a missing library with no hold is something the start page can ask for`() {
        val missing = startPageGate(parts(availability = TaffyPartAvailability.MISSING))
        val downloading = startPageGate(parts(availability = TaffyPartAvailability.PARTIAL))
        val blocked = startPageGate(parts(hold = TaffyPartHold.NOT_PUBLISHED))

        assertEquals(TaffyPartId("python-stdlib"), missing.partId)
        assertTrue(missing.canRetry)
        assertFalse(downloading.canRetry)
        assertFalse(blocked.canRetry)
    }

    @Test
    fun `a retryable hold is a retry, and a catalog defect is not`() {
        val retryable = startPageGate(
            parts(hold = TaffyPartHold.WRONG_CONTENTS),
        )
        val blocked = startPageGate(
            parts(hold = TaffyPartHold.NOT_PUBLISHED),
        )

        assertTrue(retryable.canRetry)
        assertFalse(blocked.canRetry)
    }

    private fun parts(
        availability: TaffyPartAvailability = TaffyPartAvailability.MISSING,
        downloadedBytes: Long = 0,
        totalBytes: Long = 1_000,
        hold: TaffyPartHold? = null,
    ) = TaffyPartsState(
        answered = true,
        supported = true,
        parts = listOf(library(availability, downloadedBytes, totalBytes, hold)),
    )

    private fun library(
        availability: TaffyPartAvailability,
        downloadedBytes: Long = 0,
        totalBytes: Long = 1_000,
        hold: TaffyPartHold? = null,
    ) = TaffyPart(
        id = TaffyPartId("python-stdlib"),
        version = "3.14.7-taffy.1",
        purpose = TaffyPartPurpose.PYTHON_LIBRARY,
        availability = availability,
        downloadedBytes = downloadedBytes,
        totalBytes = totalBytes,
        attempts = 0,
        hold = hold,
    )
}
