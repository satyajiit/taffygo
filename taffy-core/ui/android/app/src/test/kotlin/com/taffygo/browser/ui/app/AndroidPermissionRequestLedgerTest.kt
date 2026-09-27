// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.PlatformPermission

class AndroidPermissionRequestLedgerTest {
    @Test
    fun `ledger is bounded deduplicated reusable and close once`() {
        val ledger = AndroidPermissionRequestLedger(maxPending = 2, firstRequestCode = 100)
        val first = ledger.admit("first", PlatformPermission.CAMERA)
        val second = ledger.admit("second", PlatformPermission.LOCATION)

        assertTrue(first is PendingPermissionAdmission.Accepted && first.requestCode == 100)
        assertTrue(second is PendingPermissionAdmission.Accepted && second.requestCode == 101)
        assertSame(
            PendingPermissionAdmission.Duplicate,
            ledger.admit("first", PlatformPermission.CAMERA),
        )
        assertSame(
            PendingPermissionAdmission.Full,
            ledger.admit("third", PlatformPermission.MICROPHONE),
        )

        assertEquals(
            PendingAndroidPermission("first", PlatformPermission.CAMERA),
            ledger.settle(100),
        )
        val reused = ledger.admit("third", PlatformPermission.MICROPHONE)
        assertTrue(reused is PendingPermissionAdmission.Accepted && reused.requestCode == 100)

        ledger.close()
        ledger.close()
        assertNull(ledger.settle(100))
        assertSame(
            PendingPermissionAdmission.Closed,
            ledger.admit("fourth", PlatformPermission.CAMERA),
        )
    }

    @Test
    fun `rollback releases both request identity and request code`() {
        val ledger = AndroidPermissionRequestLedger(maxPending = 1, firstRequestCode = 500)
        val admission = ledger.admit("camera", PlatformPermission.CAMERA)
        assertEquals(PendingPermissionAdmission.Accepted(500), admission)

        ledger.rollback(500)

        assertEquals(
            PendingPermissionAdmission.Accepted(500),
            ledger.admit("camera", PlatformPermission.CAMERA),
        )
    }
}
