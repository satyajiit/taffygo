// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.chrome.browser.profiles.Profile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadActionReceiverTest {
    private val request = TaffyDownloadControlRequest(
        profileToken = "r".repeat(43),
        downloadId = DownloadId("15:legacy_downloadone"),
        action = DownloadAction.PAUSE,
    )

    @Test
    fun nativeReadyLoadsTheExactOpaqueProfileBeforeDispatch() {
        val profile = mock(Profile::class.java)
        var loadedToken = ""
        var runtimeProfile: Profile? = null
        var dispatched: TaffyDownloadControlRequest? = null
        var finishes = 0

        routeDownloadControlAfterNative(
            request = request,
            isFinished = { false },
            finish = { finishes++ },
            loadProfile = { token, callback ->
                loadedToken = token
                callback(profile)
            },
            requireRuntime = { runtimeProfile = it },
            dispatch = {
                dispatched = it
                true
            },
        )

        assertEquals(request.profileToken, loadedToken)
        assertSame(profile, runtimeProfile)
        assertEquals(request, dispatched)
        assertEquals(1, finishes)
    }

    @Test
    fun missingProfileAndLoaderFailureBothFinishWithoutDispatch() {
        var dispatches = 0
        var finishes = 0
        routeDownloadControlAfterNative(
            request = request,
            isFinished = { false },
            finish = { finishes++ },
            loadProfile = { _, callback -> callback(null) },
            dispatch = {
                dispatches++
                true
            },
        )
        routeDownloadControlAfterNative(
            request = request,
            isFinished = { false },
            finish = { finishes++ },
            loadProfile = { _, _ -> throw IllegalStateException("native unavailable") },
            dispatch = {
                dispatches++
                true
            },
        )

        assertEquals(0, dispatches)
        assertEquals(2, finishes)
    }

    @Test
    fun callbackAfterReceiverTimeoutCannotRestoreOrDispatch() {
        val profile = mock(Profile::class.java)
        var callback: ((Profile?) -> Unit)? = null
        var timedOut = false
        var restored = false
        var dispatched = false

        routeDownloadControlAfterNative(
            request = request,
            isFinished = { timedOut },
            finish = {},
            loadProfile = { _, loaded -> callback = loaded },
            requireRuntime = { restored = true },
            dispatch = {
                dispatched = true
                true
            },
        )
        assertFalse(restored)
        assertFalse(dispatched)

        timedOut = true
        requireNotNull(callback)(profile)

        assertTrue(timedOut)
        assertFalse(restored)
        assertFalse(dispatched)
    }

    @Test
    fun withdrawnRuntimeFailsClosedAndStillFinishes() {
        val profile = mock(Profile::class.java)
        var dispatched = false
        var finishes = 0

        routeDownloadControlAfterNative(
            request = request,
            isFinished = { false },
            finish = { finishes++ },
            loadProfile = { _, callback -> callback(profile) },
            requireRuntime = { throw IllegalStateException("profile closing") },
            dispatch = {
                dispatched = true
                true
            },
        )

        assertFalse(dispatched)
        assertEquals(1, finishes)
    }
}
