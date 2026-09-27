// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting

/**
 * Retains the expensive host-list half of one filtering posture projection.
 *
 * The filtering plane publishes posture, ruleset, and coalesced count changes
 * through one callback. Only a posture revision can change the exception
 * hosts, so count-only publications reuse the immutable prior list instead of
 * crossing and allocating every host through JNI again.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class FilteringPostureCache {
    private var revision: Long? = null
    private var hosts: List<String> = emptyList()

    fun hostsFor(postureRevision: Long, readHosts: () -> Array<String>): List<String> {
        if (revision != postureRevision) {
            hosts = readHosts().toList()
            revision = postureRevision
        }
        return hosts
    }

    fun clear() {
        revision = null
        hosts = emptyList()
    }
}
