// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.Context
import android.content.Intent
import androidx.test.core.app.ApplicationProvider
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.shell.TaffyDownloadNotification.Action
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Shadows.shadowOf

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadActionPayloadTest {
    private val context: Context = ApplicationProvider.getApplicationContext()
    private val profileToken = "p".repeat(43)
    private val downloadId = DownloadId("download:opaque-1")

    @Test
    fun everyControlUsesAnExplicitContentFreeIntentAndRoundTripsExactly() {
        val expected = mapOf(
            Action.PAUSE to DownloadAction.PAUSE,
            Action.RESUME to DownloadAction.RESUME,
            Action.CANCEL to DownloadAction.CANCEL,
        )

        for ((action, downloadAction) in expected) {
            val intent = savedIntent(action)
            assertEquals(TaffyDownloadActionReceiver::class.java.name, intent.component?.className)
            assertEquals(context.packageName, intent.component?.packageName)
            assertFalse(intent.dataString.orEmpty().contains(profileToken))
            assertFalse(intent.dataString.orEmpty().contains(downloadId.value))
            assertEquals(
                TaffyDownloadControlRequest(profileToken, downloadId, downloadAction),
                TaffyDownloadActionPayload.parse(intent),
            )
        }
        assertEquals(3, expected.size)
    }

    @Test
    fun actionAndDownloadIdentityProduceDistinctPendingIntentKeys() {
        val pause = savedIntent(Action.PAUSE)
        val resume = savedIntent(Action.RESUME)
        val other = shadowOf(
            TaffyDownloadActionPayload.pendingIntent(
                context,
                profileToken,
                DownloadId("download:opaque-2"),
                Action.PAUSE,
            ),
        ).savedIntent

        assertNotEquals(pause.data, resume.data)
        assertNotEquals(pause.data, other.data)
    }

    @Test
    fun malformedOrOversizedPayloadsFailClosedBeforeStartup() {
        val valid = savedIntent(Action.PAUSE)
        assertNull(TaffyDownloadActionPayload.parse(Intent(valid).setAction("other")))
        assertNull(
            TaffyDownloadActionPayload.parse(
                Intent(valid).putExtra(
                    TaffyDownloadActionPayload.PROFILE_TOKEN_EXTRA,
                    "not-an-opaque-token",
                ),
            ),
        )
        assertNull(
            TaffyDownloadActionPayload.parse(
                Intent(valid).putExtra(TaffyDownloadActionPayload.DOWNLOAD_TOKEN_EXTRA, "x".repeat(257)),
            ),
        )
        assertNull(
            TaffyDownloadActionPayload.parse(
                Intent(valid).putExtra(
                    TaffyDownloadActionPayload.CONTROL_EXTRA,
                    DownloadAction.OPEN.name,
                ),
            ),
        )
        assertThrows(IllegalArgumentException::class.java) {
            TaffyDownloadActionPayload.pendingIntent(
                context,
                "short",
                downloadId,
                Action.CANCEL,
            )
        }
    }

    @Test
    fun notificationTagsAreStableProfilePartitionedAndRevealNoRawIdentity() {
        val first = TaffyDownloadActionPayload.notificationTag(profileToken, downloadId)
        val repeat = TaffyDownloadActionPayload.notificationTag(profileToken, downloadId)
        val otherDownload = TaffyDownloadActionPayload.notificationTag(
            profileToken,
            DownloadId("download:opaque-2"),
        )
        val otherProfile = TaffyDownloadActionPayload.notificationTag(
            "q".repeat(43),
            downloadId,
        )

        assertEquals(first, repeat)
        assertNotEquals(first, otherDownload)
        assertNotEquals(first, otherProfile)
        assertTrue(first.startsWith(TaffyDownloadActionPayload.notificationProfilePrefix(profileToken)))
        assertFalse(first.contains(profileToken))
        assertFalse(first.contains(downloadId.value))
    }

    private fun savedIntent(action: Action): Intent = shadowOf(
        TaffyDownloadActionPayload.pendingIntent(context, profileToken, downloadId, action),
    ).savedIntent
}
