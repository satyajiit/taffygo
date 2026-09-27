// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadShareTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val share = TaffyDownloadShare(context)

    @Test
    fun contentUriCarriesOneReadGrantInBothAndroidChannels() {
        val uri = Uri.parse("content://downloads/report")
        val intent = share.createSendIntent("report.csv", "text/csv", uri)

        assertNotNull(intent)
        requireNotNull(intent)
        assertEquals(Intent.ACTION_SEND, intent.action)
        assertEquals("text/csv", intent.type)
        @Suppress("DEPRECATION")
        val stream = intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM)
        assertEquals(uri, stream)
        assertEquals(uri, intent.clipData?.getItemAt(0)?.uri)
        assertTrue(intent.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0)
    }

    @Test
    fun nonContentUriIsRefusedBeforeAndroidReceivesIt() {
        assertNull(
            share.createSendIntent(
                "private.txt",
                null,
                Uri.parse("file:///private/private.txt"),
            ),
        )
    }
}
