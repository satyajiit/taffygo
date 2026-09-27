// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

/**
 * The logging facade. Diagnostic logging is a developer tool, so it takes a
 * compiled-in message and never a page title, a URL, a prompt, or a model
 * output — the content-free rule of android-app-architecture section 1.6 holds
 * here as well as in `:core:analytics`.
 */
interface Logger {
    /** Record something that happened, at [level], under [tag]. */
    fun log(level: LogLevel, tag: String, message: String, cause: Throwable? = null)
}
