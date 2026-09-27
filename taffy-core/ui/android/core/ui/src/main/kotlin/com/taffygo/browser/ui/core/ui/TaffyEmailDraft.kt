// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import android.annotation.SuppressLint
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build

/**
 * Hand a written draft to the person's own email app, addressed to
 * [TaffyProjectContact.EMAIL].
 *
 * Nothing is sent. `ACTION_SENDTO` with a `mailto:` address reaches only apps
 * that write email, and the person reads, changes and sends the draft there or
 * throws it away. The subject and body travel twice — in the address and as
 * extras — because mail apps disagree about which one they read.
 *
 * @return false when no app on this phone took the draft, so the surface can
 *   say where to write instead of looking as though something happened.
 */
// `Uri.parse` rather than `toUri`: GN's core_ui_java has no androidx.core dependency.
@SuppressLint("UseKtx")
fun openEmailDraft(context: Context, subject: String, body: String): Boolean {
    val draft = Intent(
        Intent.ACTION_SENDTO,
        Uri.parse(TaffyProjectContact.emailDraftUri(subject, body)),
    ).apply {
        putExtra(Intent.EXTRA_EMAIL, arrayOf(TaffyProjectContact.EMAIL))
        putExtra(Intent.EXTRA_SUBJECT, subject)
        putExtra(Intent.EXTRA_TEXT, body)
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    }
    return try {
        context.startActivity(draft)
        true
    } catch (_: ActivityNotFoundException) {
        false
    } catch (_: SecurityException) {
        false
    }
}

/**
 * This package's own version name, or null where the platform will not say.
 * It is the number About shows, read the way Android reports it rather than
 * compiled in, so a draft names the build the person is actually running.
 */
fun installedVersionName(context: Context): String? = try {
    val info = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        context.packageManager.getPackageInfo(
            context.packageName,
            PackageManager.PackageInfoFlags.of(0),
        )
    } else {
        @Suppress("DEPRECATION")
        context.packageManager.getPackageInfo(context.packageName, 0)
    }
    info.versionName?.trim()?.takeIf(String::isNotEmpty)
} catch (_: PackageManager.NameNotFoundException) {
    null
}
