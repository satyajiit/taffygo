// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.result.contract.ActivityResultContract
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

/** One trusted Storage Access Framework destination request with a content-free name. */
internal class CreateProfileDataExportDocument : ActivityResultContract<Unit, Uri?>() {
    override fun createIntent(context: Context, input: Unit): Intent =
        Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(PROFILE_DATA_MIME_TYPE)
            .putExtra(Intent.EXTRA_TITLE, PROFILE_DATA_FILE_NAME)

    override fun parseResult(resultCode: Int, intent: Intent?): Uri? =
        if (resultCode == Activity.RESULT_OK) intent?.data else null
}

/** Writes only the exact bounded bytes rendered before this call. */
internal class ProfileDataExportDocumentWriter(
    private val resolver: ContentResolver,
    private val ioDispatcher: CoroutineDispatcher,
) {
    suspend fun write(uri: Uri, content: ByteArray): Boolean = withContext(ioDispatcher) {
        try {
            resolver.openOutputStream(uri, "w")?.use { output ->
                output.write(content)
                output.flush()
            } != null
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: IOException) {
            false
        } catch (_: SecurityException) {
            false
        } catch (_: RuntimeException) {
            false
        }
    }
}

internal const val PROFILE_DATA_MIME_TYPE = "application/json"
internal const val PROFILE_DATA_FILE_NAME = "taffygo-personal-data.json"
