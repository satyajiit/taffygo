// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import java.io.Closeable

/** Process-only route from explicit PendingIntents to the matching live regular profile. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
object TaffyDownloadActionRegistry {
    fun interface Handler {
        fun perform(request: TaffyDownloadControlRequest): Boolean
    }

    private val handlers = mutableMapOf<String, Handler>()

    fun register(profileToken: String, handler: Handler): Closeable {
        synchronized(this) {
            check(profileToken !in handlers) { "The download profile already has an action owner" }
            handlers[profileToken] = handler
        }
        return Closeable {
            synchronized(this) {
                if (handlers[profileToken] === handler) handlers.remove(profileToken)
            }
        }
    }

    fun dispatch(request: TaffyDownloadControlRequest): Boolean {
        val handler = synchronized(this) { handlers[request.profileToken] } ?: return false
        return handler.perform(request)
    }
}
