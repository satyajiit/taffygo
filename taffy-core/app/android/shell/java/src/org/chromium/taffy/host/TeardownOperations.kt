// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.annotation.VisibleForTesting
import java.io.Closeable

/** Closes a whole ownership tier even when one child reports a teardown failure. */
internal fun closeAllOwnedResources(resources: Iterable<Closeable>) {
    var failure: Throwable? = null
    for (resource in resources) {
        try {
            resource.close()
        } catch (closeFailure: Throwable) {
            failure?.addSuppressed(closeFailure) ?: run { failure = closeFailure }
        }
    }
    failure?.let { throw it }
}

/** Runs every teardown leg and reports the first failure with the rest attached. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun runAllTeardownOperations(operations: Iterable<() -> Unit>) {
    var failure: Throwable? = null
    for (operation in operations) {
        try {
            operation()
        } catch (closeFailure: Throwable) {
            failure?.addSuppressed(closeFailure) ?: run { failure = closeFailure }
        }
    }
    failure?.let { throw it }
}
