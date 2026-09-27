// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import androidx.annotation.VisibleForTesting
import org.chromium.base.ContextUtils

/** Opens one authorized task PDF through Android, without launching Chrome's own activity. */
class TaffyTaskPdfHandoff(private val context: Context) {
    /** True means Android accepted the chooser launch, not that a viewer rendered the document. */
    fun open(fileName: String, uri: Uri): Boolean {
        val activity = ContextUtils.activityFromContext(context)
        if (activity != null && (activity.isFinishing || activity.isDestroyed)) return false
        return try {
            val chooser = createChooser(fileName, uri) ?: return false
            if (activity == null) chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(chooser)
            true
        } catch (_: ActivityNotFoundException) {
            false
        } catch (_: SecurityException) {
            false
        }
    }

    @VisibleForTesting
    fun createChooser(fileName: String, uri: Uri): Intent? {
        if (uri.scheme != "content" || uri.authority.isNullOrEmpty() || uri.path.isNullOrEmpty()) {
            return null
        }
        val target = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/pdf")
            clipData = ClipData.newUri(context.contentResolver, fileName, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        @Suppress("DEPRECATION")
        val handlers = context.packageManager.queryIntentActivities(target, PackageManager.MATCH_DEFAULT_ONLY)
        if (handlers.none {
            val activity = it.activityInfo
            activity != null && activity.packageName != context.packageName &&
                activity.exported && activity.enabled && activity.applicationInfo?.enabled == true
        }) return null

        // Taffy accepts ordinary content links as a browser. Exclude its own
        // handlers so choosing a PDF viewer cannot return to the inline route.
        val ownHandlers = handlers.mapNotNull { it.activityInfo }.filter {
            it.packageName == context.packageName
        }.map { ComponentName(it.packageName, it.name) }.distinct().toTypedArray()
        return Intent.createChooser(target, null).apply {
            putExtra(Intent.EXTRA_EXCLUDE_COMPONENTS, ownHandlers)
            clipData = target.clipData
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
    }
}
