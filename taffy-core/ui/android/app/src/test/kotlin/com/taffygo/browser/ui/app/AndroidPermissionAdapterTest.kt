// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.api.PlatformPermissionRequester
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission

class AndroidPermissionAdapterTest {

    @Test
    fun `fresh denial is requestable and rationale denial stays person-started`() {
        val fresh = projectDeniedPermissionSnapshot(
            showRationale = false,
            userDecisionRecorded = false,
        )
        val rationale = projectDeniedPermissionSnapshot(
            showRationale = true,
            userDecisionRecorded = true,
        )

        assertTrue(fresh.canRequest)
        assertFalse(fresh.shouldShowRationale)
        assertTrue(rationale.canRequest)
        assertTrue(rationale.shouldShowRationale)
    }

    @Test
    fun `recorded denial without rationale requires system settings`() {
        val denied = projectDeniedPermissionSnapshot(
            showRationale = false,
            userDecisionRecorded = true,
        )

        assertEquals(PermissionDecision.DENIED, denied.decision)
        assertFalse(denied.canRequest)
        assertTrue(denied.requiresSystemSettings)
    }

    @Test
    fun `direct ledger admits only one prompt and consumes its exact result`() {
        val ledger = DirectPermissionRequestLedger()
        val delivered = mutableListOf<PlatformPermissionRequester.Snapshot>()
        val first = ledger.admit(PlatformPermission.NOTIFICATIONS) { delivered += it }

        assertTrue(first is DirectPermissionAdmission.Accepted)
        assertEquals(
            DirectPermissionAdmission.Busy,
            ledger.admit(PlatformPermission.CAMERA) { delivered += it },
        )

        ledger.settle()?.deliver(granted())
        assertEquals(listOf(granted()), delivered)
    }

    @Test
    fun `cancelled direct caller keeps the request-code claim but drops its callback`() {
        val ledger = DirectPermissionRequestLedger()
        var delivered = false
        val admission = ledger.admit(PlatformPermission.NOTIFICATIONS) { delivered = true }
            as DirectPermissionAdmission.Accepted

        ledger.detach(admission.identity)
        assertEquals(DirectPermissionAdmission.Busy, ledger.admit(PlatformPermission.CAMERA) {})
        ledger.settle()?.deliver(granted())

        assertFalse(delivered)
        assertTrue(ledger.admit(PlatformPermission.CAMERA) {} is DirectPermissionAdmission.Accepted)
    }

    @Test
    fun `stale failure cannot consume a newer direct request and close is terminal`() {
        val ledger = DirectPermissionRequestLedger()
        val first = ledger.admit(PlatformPermission.NOTIFICATIONS) {}
            as DirectPermissionAdmission.Accepted

        assertNull(ledger.fail(first.identity + 1))
        assertEquals(first.identity, ledger.takeAndClose()?.identity)
        assertEquals(
            DirectPermissionAdmission.Closed,
            ledger.admit(PlatformPermission.NOTIFICATIONS) {},
        )
    }

    private fun granted() = PlatformPermissionRequester.Snapshot(
        PermissionDecision.GRANTED,
        canRequest = false,
        shouldShowRationale = false,
    )
}
