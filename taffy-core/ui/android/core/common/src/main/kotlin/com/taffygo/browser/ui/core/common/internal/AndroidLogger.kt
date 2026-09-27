// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.internal

import android.util.Log
import com.taffygo.browser.ui.core.common.LogLevel
import com.taffygo.browser.ui.core.common.Logger
import javax.inject.Inject
import javax.inject.Singleton

/** The one place `android.util.Log` is named. */
@Singleton
internal class AndroidLogger @Inject constructor() : Logger {
    override fun log(level: LogLevel, tag: String, message: String, cause: Throwable?) {
        when (level) {
            LogLevel.DEBUG -> Log.d(tag, message, cause)
            LogLevel.WARN -> Log.w(tag, message, cause)
            LogLevel.ERROR -> Log.e(tag, message, cause)
        }
    }
}
