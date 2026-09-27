// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.analytics

import com.taffygo.browser.ui.core.common.LogLevel
import com.taffygo.browser.ui.core.common.Logger

/** A logger that keeps every line, so a test can assert what was written. */
internal class RecordingLogger(private val lines: MutableList<String>) : Logger {
    override fun log(level: LogLevel, tag: String, message: String, cause: Throwable?) {
        lines += "${level.name}: $message"
    }
}
