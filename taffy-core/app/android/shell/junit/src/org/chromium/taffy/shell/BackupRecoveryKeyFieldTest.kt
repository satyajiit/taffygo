// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.os.Build
import android.view.View
import android.view.inputmethod.EditorInfo
import com.taffygo.browser.ui.app.BackupRecoveryKeyField
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment

@RunWith(BaseRobolectricTestRunner::class)
class BackupRecoveryKeyFieldTest {
    @Test
    fun `generated handoff is wiped while only the unsaved view displays the key`() {
        val field = field()
        val session = FakeSession(BackupRecoveryKeySession.Mode.CREATE)

        assertTrue(field.showGeneratedKey(session))
        assertTrue(session.generated.all { it == '\u0000' })
        assertEquals(SyntheticKey, field.text.toString())
        assertFalse(field.isSaveEnabled)
        assertFalse(field.isSaveFromParentEnabled)
        assertFalse(field.isLongClickable)
        assertEquals(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS, field.importantForAutofill)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            assertEquals(
                View.IMPORTANT_FOR_CONTENT_CAPTURE_NO_EXCLUDE_DESCENDANTS,
                field.importantForContentCapture,
            )
        }
        field.clearSensitiveText()
        assertTrue(field.text.isNullOrEmpty())
    }

    @Test
    fun `submitted text and native handoff are cleared on acceptance and refusal`() {
        for (outcome in BackupRecoveryKeySession.Acceptance.entries) {
            val field = field()
            val session = FakeSession().apply { acceptance = outcome }
            field.setText(SyntheticKey)

            assertEquals(outcome, field.submitEnteredKey(session))
            assertTrue(session.receivedExpectedKey)
            assertNotNull(session.received)
            assertTrue(session.received!!.all { it == '\u0000' })
            assertTrue(field.text.isNullOrEmpty())
        }
    }

    @Test
    fun `native failure also clears both copies and is unavailable`() {
        val field = field()
        val session = FakeSession().apply { fail = true }
        field.setText(SyntheticKey)

        assertEquals(BackupRecoveryKeySession.Acceptance.UNAVAILABLE, field.submitEnteredKey(session))
        assertTrue(session.received!!.all { it == '\u0000' })
        assertTrue(field.text.isNullOrEmpty())
    }

    @Test
    fun `extra pasted characters are not truncated into a valid key`() {
        val field = field()
        val session = FakeSession()
        field.setText(SyntheticKey + "extra")

        field.submitEnteredKey(session)

        assertEquals(SyntheticKey.length + 5, session.receivedLength)
        assertFalse(session.receivedExpectedKey)
        assertTrue(field.imeOptions and EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING != 0)
    }

    @Test
    fun `missing or oversized generated text does not become a confirmable display`() {
        val field = field()
        val session = FakeSession(BackupRecoveryKeySession.Mode.CREATE)
        session.generated = CharArray(257) { 'x' }

        assertFalse(field.showGeneratedKey(session))
        assertTrue(session.generated.all { it == '\u0000' })
        assertTrue(field.text.isNullOrEmpty())
        session.missing = true
        assertFalse(field.showGeneratedKey(session))
    }

    private fun field() = BackupRecoveryKeyField(RuntimeEnvironment.getApplication())

    private class FakeSession(
        override val mode: BackupRecoveryKeySession.Mode = BackupRecoveryKeySession.Mode.RESTORE,
    ) : BackupRecoveryKeySession {
        var generated = SyntheticKey.toCharArray()
        var acceptance = BackupRecoveryKeySession.Acceptance.ACCEPTED
        var received: CharArray? = null
        var receivedLength = 0
        var receivedExpectedKey = false
        var fail = false
        var missing = false

        override fun takeGeneratedKeyForDisplay() = if (missing) null else generated
        override fun confirmKeyRetained() = acceptance
        override fun acceptEnteredKey(text: CharArray): BackupRecoveryKeySession.Acceptance {
            received = text
            receivedLength = text.size
            receivedExpectedKey = text.contentEquals(SyntheticKey.toCharArray())
            if (fail) throw IllegalStateException()
            return acceptance
        }
        override fun cancel() = Unit
    }

    private companion object {
        const val SyntheticKey = "TAFFY1-00010203-04050607-08090A0B-0C0D0E0F-10111213-14151617-18191A1B-1C1D1E1F"
    }
}
