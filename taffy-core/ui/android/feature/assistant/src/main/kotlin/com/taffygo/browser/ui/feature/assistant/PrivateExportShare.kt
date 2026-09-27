// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import java.io.File
import java.io.IOException

/** Writes one exact export into bounded private cache and returns a read-only content URI. */
internal fun createPrivateExportShareUri(
    context: Context,
    cacheDirectory: String,
    suggestedName: String,
    content: ByteArray,
): Uri? = try {
    val directory = File(context.cacheDir, cacheDirectory)
    if ((!directory.exists() && !directory.mkdirs()) || !directory.isDirectory) return null
    val fileName = safeExportFileName(suggestedName)
    val target = File(directory, fileName)
    target.outputStream().buffered().use { output ->
        output.write(content)
        output.flush()
    }
    directory.listFiles()
        ?.asSequence()
        ?.filter { candidate -> candidate.isFile && candidate != target }
        ?.sortedByDescending(File::lastModified)
        ?.drop(MAX_RETAINED_SHARE_FILES - 1)
        ?.forEach(File::delete)
    FileProvider.getUriForFile(context, "${context.packageName}.FileProvider", target)
} catch (_: IOException) {
    null
} catch (_: IllegalArgumentException) {
    null
} catch (_: SecurityException) {
    null
} catch (_: RuntimeException) {
    null
}

/** Opens Android's chooser with one granted URI and no export bytes in Binder extras. */
internal fun launchPrivateExportChooser(
    context: Context,
    mimeType: String,
    suggestedName: String,
    uri: Uri,
    chooserTitle: String,
): Boolean {
    val send = Intent(Intent.ACTION_SEND).apply {
        type = mimeType
        clipData = ClipData.newRawUri(suggestedName, uri)
        putExtra(Intent.EXTRA_STREAM, uri)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    return try {
        context.startActivity(
            Intent.createChooser(send, chooserTitle).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
        true
    } catch (_: ActivityNotFoundException) {
        false
    } catch (_: SecurityException) {
        false
    } catch (_: RuntimeException) {
        false
    }
}

private fun safeExportFileName(value: String): String {
    val sanitized = value
        .map { character ->
            if (character.isLetterOrDigit() || character in SAFE_FILE_NAME_PUNCTUATION) {
                character
            } else {
                '_'
            }
        }
        .joinToString(separator = "")
        .trim('.', '_')
        .take(MAX_SHARE_FILE_NAME_CHARS)
    return sanitized.ifEmpty { FALLBACK_SHARE_FILE_NAME }
}

private const val MAX_RETAINED_SHARE_FILES = 8
private const val MAX_SHARE_FILE_NAME_CHARS = 96
private const val FALLBACK_SHARE_FILE_NAME = "taffy-export.bin"
private const val SAFE_FILE_NAME_PUNCTUATION = ".-_"
