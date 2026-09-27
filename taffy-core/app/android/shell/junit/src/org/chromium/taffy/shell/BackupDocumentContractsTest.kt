// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Activity
import android.content.ClipData
import android.content.Intent
import android.net.Uri
import com.taffygo.browser.ui.app.CreateBackupDocument
import com.taffygo.browser.ui.app.OpenBackupDocument
import com.taffygo.browser.ui.app.SelectBackupDocumentForDeletion
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment

@RunWith(BaseRobolectricTestRunner::class)
class BackupDocumentContractsTest {
    private val create = CreateBackupDocument()
    private val open = OpenBackupDocument()
    private val delete = SelectBackupDocumentForDeletion()
    private val selected = Uri.parse("content://documents.example/archive/42")

    @Test
    fun `create asks for a new document with no identifying metadata`() {
        val intent = create.createIntent(RuntimeEnvironment.getApplication(), Unit)

        assertEquals(Intent.ACTION_CREATE_DOCUMENT, intent.action)
        assertEquals(setOf(Intent.CATEGORY_OPENABLE), intent.categories)
        assertEquals("application/vnd.taffygo.backup", intent.type)
        assertEquals("taffygo-backup.aib", intent.getStringExtra(Intent.EXTRA_TITLE))
        assertEquals(setOf(Intent.EXTRA_TITLE), intent.extras?.keySet())
        assertNull(intent.data)
        assertNull(intent.clipData)
        assertFalse(intent.getBooleanExtra(Intent.EXTRA_ALLOW_MULTIPLE, false))
        assertEquals(0, intent.flags and Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
    }

    @Test
    fun `restore asks for one openable archive without retaining its grant`() {
        val intent = open.createIntent(RuntimeEnvironment.getApplication(), Unit)

        assertEquals(Intent.ACTION_OPEN_DOCUMENT, intent.action)
        assertEquals(setOf(Intent.CATEGORY_OPENABLE), intent.categories)
        assertEquals("application/vnd.taffygo.backup", intent.type)
        assertNull(intent.extras)
        assertNull(intent.data)
        assertNull(intent.clipData)
        assertEquals(0, intent.flags and Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
    }

    @Test
    fun `deletion selection requests transient read and write access only`() {
        val intent = delete.createIntent(RuntimeEnvironment.getApplication(), Unit)

        assertEquals(Intent.ACTION_OPEN_DOCUMENT, intent.action)
        assertEquals(setOf(Intent.CATEGORY_OPENABLE), intent.categories)
        assertEquals("application/vnd.taffygo.backup", intent.type)
        assertNull(intent.extras)
        assertNull(intent.data)
        assertNull(intent.clipData)
        assertFalse(intent.getBooleanExtra(Intent.EXTRA_ALLOW_MULTIPLE, false))
        assertTrue(intent.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0)
        assertTrue(intent.flags and Intent.FLAG_GRANT_WRITE_URI_PERMISSION != 0)
        assertEquals(0, intent.flags and Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
    }

    @Test
    fun `both contracts accept only the successful selected content document`() {
        val result = Intent().setData(selected)

        assertEquals(selected, create.parseResult(Activity.RESULT_OK, result))
        assertEquals(selected, open.parseResult(Activity.RESULT_OK, result))
        for (code in listOf(Activity.RESULT_CANCELED, Activity.RESULT_FIRST_USER)) {
            assertNull(create.parseResult(code, result))
            assertNull(open.parseResult(code, result))
        }
    }

    @Test
    fun `missing results and non content URIs cannot become document access`() {
        for (uri in listOf(null, "file:///tmp/archive.aib", "https://example.test/archive.aib", "archive.aib")) {
            val result = Intent().setData(uri?.let(Uri::parse))
            assertNull(create.parseResult(Activity.RESULT_OK, result))
            assertNull(open.parseResult(Activity.RESULT_OK, result))
        }
        assertNull(create.parseResult(Activity.RESULT_OK, null))
        assertNull(open.parseResult(Activity.RESULT_OK, null))
    }

    @Test
    fun `unrequested clip data cannot replace the selected document`() {
        val result = Intent().apply { clipData = ClipData.newRawUri("archive", selected) }

        assertNull(create.parseResult(Activity.RESULT_OK, result))
        assertNull(open.parseResult(Activity.RESULT_OK, result))
    }

    @Test
    fun `deletion selection requires one content document with both returned grants`() {
        val both = Intent.FLAG_GRANT_READ_URI_PERMISSION or
            Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        val granted = Intent().setData(selected).addFlags(both)

        assertEquals(selected, delete.parseResult(Activity.RESULT_OK, granted))
        for (flags in listOf(0, Intent.FLAG_GRANT_READ_URI_PERMISSION,
            Intent.FLAG_GRANT_WRITE_URI_PERMISSION)) {
            assertNull(
                delete.parseResult(
                    Activity.RESULT_OK,
                    Intent().setData(selected).addFlags(flags),
                ),
            )
        }
        assertNull(delete.parseResult(Activity.RESULT_CANCELED, granted))
        assertNull(
            delete.parseResult(
                Activity.RESULT_OK,
                Intent().setData(Uri.parse("file:///tmp/archive.aib")).addFlags(both),
            ),
        )
        assertNull(
            delete.parseResult(
                Activity.RESULT_OK,
                Intent().setData(Uri.parse("content:archive.aib")).addFlags(both),
            ),
        )
        assertNull(
            delete.parseResult(
                Activity.RESULT_OK,
                granted.apply { clipData = ClipData.newRawUri("archive", selected) },
            ),
        )
    }
}
