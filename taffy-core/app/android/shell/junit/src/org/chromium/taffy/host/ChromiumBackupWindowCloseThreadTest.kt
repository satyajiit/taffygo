// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.Dispatchers
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumBackupWindowCloseThreadTest {
    @Test
    fun `off UI close is rejected before session and native custody are withdrawn`() {
        val native = FakeNativeWindow()
        val host = ChromiumBackupWindowHost(
            RuntimeEnvironment.getApplication().contentResolver,
            Dispatchers.Unconfined,
            native,
        )
        assertTrue(host.activate())
        val session = requireNotNull(
            host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.CREATE),
        )
        val failure = AtomicReference<Throwable?>()
        val worker = Thread {
            try {
                host.close()
            } catch (caught: Throwable) {
                failure.set(caught)
            }
        }

        worker.start()
        worker.join()

        assertTrue(failure.get() is AssertionError)
        assertEquals(0, native.closeCalls)
        assertArrayEquals(charArrayOf('K', 'E', 'Y'), session.takeGeneratedKeyForDisplay())
        host.close()
        assertEquals(1, native.closeCalls)
    }
}
