// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.AuthSurfacePlan
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ProfilePlatformSurfaceDispatcherTest {
    @Test
    fun exactlyOneResumedWindowOwnsSurfaceUntilPauseOrClose() =
        runBlocking(Dispatchers.Main.immediate) {
            val dispatcher = ProfilePlatformSurfaceDispatcher()
            val opened = mutableListOf<String>()
            val first = dispatcher.register(
                oauth = { opened += "first"; true },
                credential = { _, _, _ -> },
            )
            val second = dispatcher.register(
                oauth = { opened += "second"; true },
                credential = { _, _, _ -> },
            )
            first.activate()
            assertTrue(dispatcher.openOAuth(plan()))
            second.activate()
            assertFalse(dispatcher.openOAuth(plan()))
            second.deactivate()
            assertTrue(dispatcher.openOAuth(plan()))
            first.close()
            assertFalse(dispatcher.openOAuth(plan()))
            assertTrue(opened == listOf("first", "first"))
        }

    @Test
    fun absentWindowRefusesCredentialWithoutOwningNonceMaterial() =
        runBlocking(Dispatchers.Main.immediate) {
            val dispatcher = ProfilePlatformSurfaceDispatcher()
            val hashedNonce = "a".repeat(64)

            assertFalse(dispatcher.openGoogleCredential("flow", "client", hashedNonce))
        }

    @Test
    fun selectedWindowReceivesThePublicHashUnchanged() =
        runBlocking(Dispatchers.Main.immediate) {
            val dispatcher = ProfilePlatformSurfaceDispatcher()
            val received = mutableListOf<String>()
            val window = dispatcher.register(
                oauth = { false },
                credential = { _, _, hashedNonce -> received += hashedNonce },
            )
            val hashedNonce = "0123456789abcdef".repeat(4)
            window.activate()

            assertTrue(dispatcher.openGoogleCredential("flow", "client", hashedNonce))
            assertEquals(listOf(hashedNonce), received)
        }

    private fun plan() = AuthSurfacePlan("flow", "https://example.test/authorize")
}
