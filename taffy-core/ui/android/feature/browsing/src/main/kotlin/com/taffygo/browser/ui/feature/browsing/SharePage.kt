// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent

/**
 * Offer the current page's exact canonical address to the system share sheet.
 *
 * Taffy never picks recipients. A tab that has been nowhere, or a page with
 * no safe HTTP(S) address, is a no-op — the URL is not invented.
 */
internal fun sharePageUrl(context: Context, canonicalUrl: String, chooserTitle: String) {
    if (canonicalUrl.isBlank()) return
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_TEXT, canonicalUrl)
    }
    try {
        context.startActivity(
            Intent.createChooser(send, chooserTitle).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
    } catch (_: ActivityNotFoundException) {
        // Nothing on this device can take a share. The tile asked; that is all
        // this build can honestly do.
    }
}
