// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import java.security.MessageDigest
import java.util.Base64
import java.util.Locale
import org.chromium.base.IntentUtils
import org.chromium.taffy.shell.TaffyDownloadNotification.Action

/** Builds and parses the bounded, content-free payload used by notification controls. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
object TaffyDownloadActionPayload {
    private const val INTENT_ACTION = "org.chromium.taffy.DOWNLOAD_CONTROL"
    const val PROFILE_TOKEN_EXTRA = "taffy.download.profile"
    const val DOWNLOAD_TOKEN_EXTRA = "taffy.download.id"
    const val CONTROL_EXTRA = "taffy.download.control"
    private const val MAX_DOWNLOAD_TOKEN_BYTES = 256
    private val profileTokenPattern = Regex("[A-Za-z0-9_-]{43}")

    fun pendingIntent(
        context: Context,
        profileToken: String,
        downloadId: DownloadId,
        action: Action,
    ): PendingIntent {
        require(isValidProfileToken(profileToken)) { "Invalid download profile token" }
        require(isValidDownloadToken(downloadId.value)) { "Invalid download identity" }
        val intent = Intent(context, TaffyDownloadActionReceiver::class.java)
            .setAction(INTENT_ACTION)
            .setData(
                Uri.Builder()
                    .scheme("taffygo")
                    .authority("download-control")
                    .appendPath(notificationTag(profileToken, downloadId))
                    .appendPath(action.name.lowercase(Locale.ROOT))
                    .build(),
            )
            .putExtra(PROFILE_TOKEN_EXTRA, profileToken)
            .putExtra(DOWNLOAD_TOKEN_EXTRA, downloadId.value)
            .putExtra(CONTROL_EXTRA, action.name)
        return PendingIntent.getBroadcast(
            context,
            0,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
    }

    fun parse(intent: Intent?): TaffyDownloadControlRequest? {
        if (intent?.action != INTENT_ACTION) return null
        val profileToken = IntentUtils.safeGetStringExtra(intent, PROFILE_TOKEN_EXTRA) ?: return null
        val downloadToken = IntentUtils.safeGetStringExtra(intent, DOWNLOAD_TOKEN_EXTRA) ?: return null
        val actionName = IntentUtils.safeGetStringExtra(intent, CONTROL_EXTRA) ?: return null
        if (!isValidProfileToken(profileToken) || !isValidDownloadToken(downloadToken)) return null
        val action = when (runCatching { Action.valueOf(actionName) }.getOrNull()) {
            Action.PAUSE -> DownloadAction.PAUSE
            Action.RESUME -> DownloadAction.RESUME
            Action.CANCEL -> DownloadAction.CANCEL
            null -> return null
        }
        return TaffyDownloadControlRequest(profileToken, DownloadId(downloadToken), action)
    }

    fun notificationTag(profileToken: String, downloadId: DownloadId): String {
        require(isValidProfileToken(profileToken)) { "Invalid download profile token" }
        require(isValidDownloadToken(downloadId.value)) { "Invalid download identity" }
        return "${notificationProfilePrefix(profileToken)}:${digestToken(downloadId.value)}"
    }

    fun notificationProfilePrefix(profileToken: String): String {
        require(isValidProfileToken(profileToken)) { "Invalid download profile token" }
        return "download:${digestToken(profileToken)}"
    }

    private fun isValidProfileToken(value: String): Boolean = profileTokenPattern.matches(value)

    private fun isValidDownloadToken(value: String): Boolean =
        value.isNotBlank() && downloadTextFits(value, MAX_DOWNLOAD_TOKEN_BYTES)

    private fun digestToken(value: String): String {
        val identity = value.encodeToByteArray()
        val digest = try {
            MessageDigest.getInstance("SHA-256").digest(identity)
        } finally {
            identity.fill(0)
        }
        return try {
            Base64.getUrlEncoder().withoutPadding().encodeToString(digest).take(16)
        } finally {
            digest.fill(0)
        }
    }
}
