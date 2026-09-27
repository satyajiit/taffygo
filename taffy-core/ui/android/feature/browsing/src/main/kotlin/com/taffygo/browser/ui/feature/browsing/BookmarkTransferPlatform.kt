// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.result.contract.ActivityResultContract
import java.io.ByteArrayOutputStream
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

/** Opens one person-selected HTML file and takes no durable provider permission. */
internal class OpenBookmarkDocument : ActivityResultContract<Unit, Uri?>() {
    override fun createIntent(context: Context, input: Unit): Intent =
        Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(BOOKMARK_DOCUMENT_MIME_TYPE)

    override fun parseResult(resultCode: Int, intent: Intent?): Uri? =
        if (resultCode == Activity.RESULT_OK) intent?.data else null
}

/** Creates one person-selected portable bookmark document. */
internal class CreateBookmarkDocument : ActivityResultContract<Unit, Uri?>() {
    override fun createIntent(context: Context, input: Unit): Intent =
        Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(BOOKMARK_DOCUMENT_MIME_TYPE)
            .putExtra(Intent.EXTRA_TITLE, BOOKMARK_DOCUMENT_FILE_NAME)

    override fun parseResult(resultCode: Int, intent: Intent?): Uri? =
        if (resultCode == Activity.RESULT_OK) intent?.data else null
}

/** Android's narrow role: copy a bounded document across one granted content URI. */
internal class BookmarkTransferPlatform(
    private val resolver: ContentResolver,
    private val ioDispatcher: CoroutineDispatcher,
) {
    suspend fun read(uri: Uri): BookmarkTransferDocument? = withContext(ioDispatcher) {
        containProviderFailure {
            val input = resolver.openInputStream(uri) ?: return@containProviderFailure null
            input.use { stream ->
                val output = ByteArrayOutputStream()
                val buffer = ByteArray(8 * 1_024)
                while (true) {
                    val count = stream.read(buffer)
                    if (count < 0) break
                    if (output.size() + count > BookmarkHtmlCodec.MAX_DOCUMENT_CHARS) {
                        return@containProviderFailure null
                    }
                    output.write(buffer, 0, count)
                }
                BookmarkHtmlCodec.decode(output.toString(Charsets.UTF_8.name()))
            }
        }
    }

    suspend fun write(uri: Uri, document: BookmarkTransferDocument): Boolean =
        withContext(ioDispatcher) {
            containProviderFailure {
                val encoded = BookmarkHtmlCodec.encode(document)
                    ?: return@containProviderFailure false
                val content = encoded.toByteArray(Charsets.UTF_8)
                resolver.openOutputStream(uri, "w")?.use { output ->
                    output.write(content)
                    output.flush()
                } != null
            } ?: false
        }

    private inline fun <T> containProviderFailure(block: () -> T): T? = try {
        block()
    } catch (cancelled: CancellationException) {
        throw cancelled
    } catch (_: IOException) {
        null
    } catch (_: SecurityException) {
        null
    } catch (_: RuntimeException) {
        null
    }
}

internal const val BOOKMARK_DOCUMENT_MIME_TYPE: String = "text/html"
internal const val BOOKMARK_DOCUMENT_FILE_NAME: String = "taffygo-bookmarks.html"
