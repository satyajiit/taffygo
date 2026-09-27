// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.ByteArrayInputStream
import java.io.FileNotFoundException
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.CancellationException
import org.junit.Assert.assertEquals
import org.junit.Assert.fail
import org.junit.Test

class BackupDocumentDeletionTest {
    @Test
    fun `only a successful delete followed by an absent URI proves deletion`() {
        assertEquals(
            BackupDocumentTransfer.DeleteResult.DELETED,
            delete { throw FileNotFoundException("absent") },
        )
    }

    @Test
    fun `a provider accepting delete while retaining bytes reports still present`() {
        assertEquals(
            BackupDocumentTransfer.DeleteResult.STILL_PRESENT,
            delete { ByteArrayInputStream(byteArrayOf(1)) },
        )
    }

    @Test
    fun `null permission loss and provider errors cannot prove absence`() {
        assertEquals(BackupDocumentTransfer.DeleteResult.UNVERIFIABLE, delete { null })
        for (failure in listOf(IOException("offline"), SecurityException("revoked"),
                               IllegalStateException("provider"))) {
            assertEquals(
                BackupDocumentTransfer.DeleteResult.UNVERIFIABLE,
                delete { throw failure },
            )
        }
    }

    @Test
    fun `delete refusal does not claim success`() {
        assertEquals(
            BackupDocumentTransfer.DeleteResult.UNVERIFIABLE,
            delete(accepted = false) { throw FileNotFoundException("absent") },
        )
    }

    @Test
    fun `cancellation is propagated`() {
        try {
            delete { throw CancellationException("cancel") }
            fail("Cancellation must reach the caller")
        } catch (_: CancellationException) {
            // Expected: no deletion success is synthesized.
        }
    }

    private fun delete(accepted: Boolean = true, reader: () -> InputStream?) =
        BackupDocumentTransfer.deleteAndVerify(object : BackupDocumentTransfer.Document {
            override fun openWriter(): OutputStream = throw UnsupportedOperationException()
            override fun openReader(): InputStream? = reader()
            override fun delete(): Boolean = accepted
        })
}
