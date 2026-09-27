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

/** Selects one existing archive with transient read/write access; selection never deletes it. */
class SelectBackupDocumentForDeletion : ActivityResultContract<Unit, Uri?>() {
    override fun createIntent(context: Context, input: Unit): Intent =
        Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(BACKUP_DOCUMENT_MIME_TYPE)
            .addFlags(REQUIRED_GRANTS)

    override fun parseResult(resultCode: Int, intent: Intent?): Uri? {
        if (resultCode != Activity.RESULT_OK || intent == null || intent.clipData != null ||
            (intent.flags and REQUIRED_GRANTS) != REQUIRED_GRANTS
        ) {
            return null
        }
        return intent.data?.takeIf {
            it.scheme == ContentResolver.SCHEME_CONTENT && !it.authority.isNullOrBlank()
        }
    }

    private companion object {
        const val REQUIRED_GRANTS =
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
    }
}
