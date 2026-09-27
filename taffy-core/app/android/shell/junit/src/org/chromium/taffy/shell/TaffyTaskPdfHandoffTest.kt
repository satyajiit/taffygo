// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.ContextWrapper
import android.content.Intent
import android.net.Uri
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric

@RunWith(BaseRobolectricTestRunner::class)
class TaffyTaskPdfHandoffTest {
    @Test
    fun `chooser carries only the local PDF and read grants and excludes our own activities`() {
        val platform = TaskPdfTestPlatform()
        val ownPackage = platform.application.packageName
        val own = platform.handler(ownPackage)
        platform.setHandlers(own, platform.handler("example.pdfreader"))

        assertTrue(platform.handoff.open("report.pdf", TaskPdfTestPlatform.URI))
        val chooser = requireNotNull(platform.takeChooser())
        assertEquals(Intent.ACTION_CHOOSER, chooser.action)
        assertTrue(chooser.flags and Intent.FLAG_ACTIVITY_NEW_TASK != 0)
        @Suppress("DEPRECATION")
        val target = requireNotNull(chooser.getParcelableExtra<Intent>(Intent.EXTRA_INTENT))
        assertEquals(Intent.ACTION_VIEW, target.action)
        assertEquals("application/pdf", target.type)
        assertEquals(TaskPdfTestPlatform.URI, target.data)
        assertNull(target.component)
        assertNull(target.`package`)
        assertNull(target.selector)
        assertTrue(target.extras == null || target.extras!!.isEmpty)
        for (intent in listOf(target, chooser)) {
            assertEquals(1, intent.clipData?.itemCount)
            assertEquals(TaskPdfTestPlatform.URI, intent.clipData?.getItemAt(0)?.uri)
            assertEquals("report.pdf", intent.clipData?.description?.label)
            assertTrue(intent.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0)
            assertEquals(0, intent.flags and (
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION or
                    Intent.FLAG_GRANT_PREFIX_URI_PERMISSION
                ))
            assertFalse(intent.hasExtra(Intent.EXTRA_STREAM))
            assertFalse(intent.hasExtra(Intent.EXTRA_REFERRER))
        }
        @Suppress("DEPRECATION")
        val excluded = chooser.getParcelableArrayExtra(Intent.EXTRA_EXCLUDE_COMPONENTS)
        assertEquals(listOf(ComponentName(ownPackage, own.activityInfo.name)), excluded?.toList())
        assertNull(platform.takeChooser())
    }

    @Test
    fun `own package or inaccessible handlers cannot make a viewer available`() {
        val platform = TaskPdfTestPlatform()
        for (mutation in 0..2) {
            val candidate = platform.handler("example.pdfreader")
            when (mutation) {
                0 -> candidate.activityInfo.exported = false
                1 -> candidate.activityInfo.enabled = false
                2 -> candidate.activityInfo.applicationInfo.enabled = false
            }
            platform.setHandlers(platform.handler(platform.application.packageName), candidate)
            assertFalse("mutation $mutation", platform.handoff.open("report.pdf", TaskPdfTestPlatform.URI))
            assertNull(platform.takeChooser())
        }
        platform.removeViewer()
        assertFalse(platform.handoff.open("report.pdf", TaskPdfTestPlatform.URI))
        assertNull(platform.takeChooser())
    }

    @Test
    fun `file network and incomplete content URIs are refused before Android launch`() {
        val platform = TaskPdfTestPlatform()
        for (address in listOf(
            "file:///private/report.pdf",
            "https://example.test/report.pdf",
            "content:///report.pdf",
            "content://downloads",
        )) {
            assertFalse(address, platform.handoff.open("report.pdf", Uri.parse(address)))
            assertNull(platform.takeChooser())
        }
    }

    @Test
    fun `Android launch refusal is returned without claiming an opened PDF`() {
        val platform = TaskPdfTestPlatform()
        for (failure in listOf(ActivityNotFoundException(), SecurityException())) {
            val context = object : ContextWrapper(platform.application) {
                override fun startActivity(intent: Intent) = throw failure
            }
            assertFalse(TaffyTaskPdfHandoff(context).open("report.pdf", TaskPdfTestPlatform.URI))
            assertNull(platform.takeChooser())
        }
    }

    @Test
    fun `a finishing browser activity cannot launch a delayed handoff`() {
        val platform = TaskPdfTestPlatform()
        val controller = Robolectric.buildActivity(Activity::class.java).setup()
        val activity = controller.get()
        activity.finish()
        assertFalse(TaffyTaskPdfHandoff(activity).open("report.pdf", TaskPdfTestPlatform.URI))
        assertNull(platform.takeChooser())
        controller.pause().stop().destroy()
    }
}
