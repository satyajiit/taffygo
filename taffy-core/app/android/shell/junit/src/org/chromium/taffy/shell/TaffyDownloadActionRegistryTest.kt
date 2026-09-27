// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import java.io.Closeable
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadActionRegistryTest {
    private val registrations = mutableListOf<Closeable>()

    @After
    fun closeRegistrations() {
        registrations.asReversed().forEach(Closeable::close)
        registrations.clear()
    }

    @Test
    fun onlyTheExactLiveProfileOwnerReceivesAControl() {
        val profileToken = "p".repeat(43)
        val request = request(profileToken)
        val received = mutableListOf<TaffyDownloadControlRequest>()
        registrations += TaffyDownloadActionRegistry.register(profileToken) { candidate ->
            received += candidate
            true
        }

        assertTrue(TaffyDownloadActionRegistry.dispatch(request))
        assertFalse(TaffyDownloadActionRegistry.dispatch(request("q".repeat(43))))
        assertEquals(listOf(request), received)
    }

    @Test
    fun closingTheProfileRevokesPendingIntentAuthority() {
        val profileToken = "r".repeat(43)
        val registration = TaffyDownloadActionRegistry.register(profileToken) { true }
        registrations += registration
        registration.close()

        assertFalse(TaffyDownloadActionRegistry.dispatch(request(profileToken)))
    }

    @Test
    fun aProfileCannotAcquireTwoActionOwners() {
        val profileToken = "s".repeat(43)
        registrations += TaffyDownloadActionRegistry.register(profileToken) { true }

        assertThrows(IllegalStateException::class.java) {
            TaffyDownloadActionRegistry.register(profileToken) { true }
        }
    }

    private fun request(profileToken: String) = TaffyDownloadControlRequest(
        profileToken = profileToken,
        downloadId = DownloadId("download:one"),
        action = DownloadAction.PAUSE,
    )
}
