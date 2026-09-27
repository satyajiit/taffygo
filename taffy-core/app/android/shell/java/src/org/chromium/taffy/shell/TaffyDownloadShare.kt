// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.annotation.VisibleForTesting

/** Trusted Android handoff for one Chromium-authorized downloaded file. */
class TaffyDownloadShare(private val context: Context) {

    fun showChooser(fileName: String, mimeType: String?, uri: Uri) {
        val send = createSendIntent(fileName, mimeType, uri) ?: return
        try {
            context.startActivity(
                Intent.createChooser(send, context.getString(R.string.taffy_download_share)),
            )
        } catch (_: ActivityNotFoundException) {
            // No receiver means sharing is unavailable. The file remains untouched.
        } catch (_: SecurityException) {
            // Refuse a provider URI Android will not grant instead of widening access.
        }
    }

    @VisibleForTesting
    fun createSendIntent(fileName: String, mimeType: String?, uri: Uri): Intent? {
        if (!uri.scheme.equals("content", ignoreCase = true)) return null
        return Intent(Intent.ACTION_SEND).apply {
            type = mimeType?.takeIf(String::isNotBlank) ?: "application/octet-stream"
            clipData = ClipData.newUri(context.contentResolver, fileName, uri)
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
    }
}
