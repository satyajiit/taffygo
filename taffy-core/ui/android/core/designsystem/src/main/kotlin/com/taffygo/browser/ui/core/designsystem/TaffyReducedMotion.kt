// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import android.provider.Settings
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalInspectionMode

/**
 * Whether the person asked for less motion.
 *
 * The UX spec's accessibility obligations and the screen catalog both require
 * "Remove animations" to be respected everywhere, and Compose has no built-in
 * signal for it. Android's accessibility toggle and the developer setting both
 * write the animator duration scale, so that is what this reads: zero means
 * the platform has been told to stop animating, and every Taffy surface that
 * loops has to honour it.
 *
 * A preview always reports false, because a still frame of a skeleton with no
 * highlight in it is not what the preview is for.
 */
@Composable
fun currentTaffyReducedMotion(): Boolean {
    if (LocalInspectionMode.current) return false
    val context = LocalContext.current
    return remember(context) {
        Settings.Global.getFloat(
            context.contentResolver,
            Settings.Global.ANIMATOR_DURATION_SCALE,
            1f,
        ) == 0f
    }
}
