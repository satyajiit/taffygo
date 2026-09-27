// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class CoreApiSubmissionLedgerTest {
    @Test
    fun acceptedSubmissionCompletesExactlyOnce() {
        val ledger = CoreApiSubmissionLedger()
        val terminals = mutableListOf<CoreApiSubmissionException.Reason?>()
        val id = ledger.register(terminals::add)

        assertTrue(ledger.complete(id))
        assertFalse(ledger.complete(id, CoreApiSubmissionException.Reason.CORE_UNAVAILABLE))
        assertEquals(listOf<CoreApiSubmissionException.Reason?>(null), terminals)
        assertEquals(0, ledger.sizeForTesting())
    }

    @Test
    fun transportDisconnectFailsEveryPendingSubmissionExactlyOnce() {
        val ledger = CoreApiSubmissionLedger()
        val terminals = mutableListOf<Pair<Long, CoreApiSubmissionException.Reason?>>()
        val first = ledger.register { terminals += 1L to it }
        val second = ledger.register { terminals += 2L to it }

        ledger.failAll(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
        ledger.failAll(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)

        assertEquals(
            listOf(
                1L to CoreApiSubmissionException.Reason.CORE_UNAVAILABLE,
                2L to CoreApiSubmissionException.Reason.CORE_UNAVAILABLE,
            ),
            terminals,
        )
        assertFalse(ledger.complete(first))
        assertFalse(ledger.complete(second))
    }

    @Test
    fun cancellationClaimsOnlyItsSubmission() {
        val ledger = CoreApiSubmissionLedger()
        var cancelledTerminal: CoreApiSubmissionException.Reason? = null
        var retainedTerminal: CoreApiSubmissionException.Reason? = null
        val cancelled = ledger.register { cancelledTerminal = it }
        val retained = ledger.register { retainedTerminal = it }

        assertTrue(ledger.cancel(cancelled))
        assertFalse(ledger.cancel(cancelled))
        assertTrue(ledger.complete(retained, CoreApiSubmissionException.Reason.BACKPRESSURE))

        assertNull(cancelledTerminal)
        assertEquals(CoreApiSubmissionException.Reason.BACKPRESSURE, retainedTerminal)
    }
}
