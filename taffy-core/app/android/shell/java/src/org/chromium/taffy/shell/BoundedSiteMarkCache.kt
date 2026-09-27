// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import java.util.LinkedHashMap

/** Small access-ordered store; a caller can never retain a profile's whole favicon history. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class BoundedSiteMarkCache<T : Any>(private val capacity: Int) {
    private val entries = object : LinkedHashMap<String, T>(capacity, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, T>?): Boolean =
            size > capacity
    }

    init {
        require(capacity > 0)
    }

    /** Reports membership and promotes a hit without allocating a new snapshot. */
    fun touch(host: String): Boolean = entries[host] != null

    fun put(host: String, value: T) {
        entries[host] = value
    }

    fun snapshot(): Map<String, T> = entries.toMap()

    fun clear() = entries.clear()
}
