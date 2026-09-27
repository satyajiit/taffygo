// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.PartMemberStatus

/**
 * What a read of one member out of an installed part answered.
 *
 * The status is kept beside the bytes rather than collapsed into a nullable
 * result, because the reasons are not equivalent: a part that has not been
 * downloaded yet is the ordinary case and a part that could not be read as the
 * container it is meant to be is a defect in what was delivered. A caller may
 * treat both as "no picture"; the ones that log or report must be able to tell
 * them apart.
 */
data class PartMemberRead(
    /** How the read ended. */
    val status: PartMemberStatus,
    /** The member's contents, present only when the status is OK. */
    val bytes: ByteArray?,
) {
    /**
     * Value equality over the contents, which a data class does not give for an
     * array. Written out because the alternative is a type whose equals()
     * silently compares identities, which is the kind of surprise a cache
     * discovers late.
     */
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is PartMemberRead) return false
        return status == other.status && bytes.contentEquals(other.bytes)
    }

    override fun hashCode(): Int = 31 * status.hashCode() + bytes.contentHashCode()
}
