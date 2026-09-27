// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.view.WindowManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.platform.LocalView
import java.util.concurrent.atomic.AtomicInteger

/**
 * Sets `FLAG_SECURE` on the host Activity while this composition is in the
 * tree, and clears it on dispose.
 *
 * Safe with no Activity (previews, tests): it does nothing. Overlapping
 * compositions share one count so disposing one does not uncover another.
 */
@Composable
fun TaffySecureWindow() {
    val view = LocalView.current
    DisposableEffect(view) {
        if (view.isInEditMode) {
            return@DisposableEffect onDispose { }
        }
        val window = view.context.findActivity()?.window
        if (window == null) {
            return@DisposableEffect onDispose { }
        }
        if (SecureWindowCount.incrementAndGet() == 1) {
            window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        }
        onDispose {
            if (SecureWindowCount.decrementAndGet() == 0) {
                window.clearFlags(WindowManager.LayoutParams.FLAG_SECURE)
            }
        }
    }
}

private val SecureWindowCount = AtomicInteger(0)

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}
