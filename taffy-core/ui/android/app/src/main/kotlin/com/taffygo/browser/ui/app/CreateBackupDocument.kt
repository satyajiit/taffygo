// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.result.contract.ActivityResultContract

/** Android chooses a new archive destination without receiving identifying metadata. */
class CreateBackupDocument : ActivityResultContract<Unit, Uri?>() {
    override fun createIntent(context: Context, input: Unit): Intent =
        Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(BACKUP_DOCUMENT_MIME_TYPE)
            .putExtra(Intent.EXTRA_TITLE, "taffygo-backup.aib")

    override fun parseResult(resultCode: Int, intent: Intent?): Uri? =
        grantedBackupDocument(resultCode, intent)
}

internal const val BACKUP_DOCUMENT_MIME_TYPE = "application/vnd.taffygo.backup"

internal fun grantedBackupDocument(resultCode: Int, intent: Intent?): Uri? =
    if (resultCode == Activity.RESULT_OK) {
        intent?.data?.takeIf { it.scheme == ContentResolver.SCHEME_CONTENT }
    } else {
        null
    }
