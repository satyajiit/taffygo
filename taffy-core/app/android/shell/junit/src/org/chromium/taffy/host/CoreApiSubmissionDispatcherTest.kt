// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.os.Handler
import android.os.Looper
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Shadows.shadowOf

/** What one command's failure does to the commands after it. */
@RunWith(BaseRobolectricTestRunner::class)
class CoreApiSubmissionDispatcherTest {

    @Test
    fun aCommandTheProxyCannotEncodeFailsThatCommandAndLeavesTheTransportUp() {
        var transportFailures = 0
        val dispatcher = CoreApiSubmissionDispatcher(
            mainHandler = Handler(Looper.getMainLooper()),
            isClosed = { false },
            onTransportFailure = { transportFailures += 1 },
        )
        var failure: CoreApiSubmissionException? = null
        val job = CoroutineScope(Dispatchers.Unconfined).launch {
            try {
                dispatcher.submit { throw IllegalStateException("a null non-nullable array") }
            } catch (refused: CoreApiSubmissionException) {
                failure = refused
            }
        }

        shadowOf(Looper.getMainLooper()).idle()

        assertTrue(job.isCompleted)
        // The command is refused by name, and nothing else is touched: the
        // pipe never saw the message, so there is no transport to fail. Read
        // as a disconnect, one unencodable start once emptied every core
        // surface for the life of the process.
        assertEquals(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION, failure?.reason)
        assertEquals(0, transportFailures)
    }

    @Test
    fun aRefusedAdmissionIsThatCommandsFailureAlone() {
        var transportFailures = 0
        val dispatcher = CoreApiSubmissionDispatcher(
            mainHandler = Handler(Looper.getMainLooper()),
            isClosed = { false },
            onTransportFailure = { transportFailures += 1 },
        )
        var failure: CoreApiSubmissionException? = null
        val job = CoroutineScope(Dispatchers.Unconfined).launch {
            try {
                dispatcher.submit { callback ->
                    callback(org.chromium.taffy.core_api.mojom.CoreApiSubmissionStatus.BACKPRESSURE)
                }
            } catch (refused: CoreApiSubmissionException) {
                failure = refused
            }
        }

        shadowOf(Looper.getMainLooper()).idle()

        assertTrue(job.isCompleted)
        assertEquals(CoreApiSubmissionException.Reason.BACKPRESSURE, failure?.reason)
        assertEquals(0, transportFailures)
    }
}
