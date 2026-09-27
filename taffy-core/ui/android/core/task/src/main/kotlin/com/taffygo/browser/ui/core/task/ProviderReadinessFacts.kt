// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

/**
 * The facts [taffyReadiness] reads. Every set names providers by id.
 *
 * All but one come from the core's roster; the held set comes from the
 * browser's own vault, which may hold a handle the core has not confirmed
 * yet. A held-but-unconfirmed key is pending, never usable.
 */
data class ProviderReadinessFacts(
    /** False until the core has published a complete projection. */
    val known: Boolean,
    /** Enabled providers whose stored credential is usable now. */
    val usableCredentialProviderIds: Set<String> = emptySet(),
    /** Enabled providers with any stored credential, usable or not. */
    val storedCredentialProviderIds: Set<String> = emptySet(),
    /** Enabled providers the person added or repointed, which need no key of ours. */
    val ownEndpointProviderIds: Set<String> = emptySet(),
    /** Providers the browser holds a credential handle for. */
    val heldCredentialProviderIds: Set<String> = emptySet(),
) {
    /**
     * The providers a request could reach now: a usable stored credential, or
     * an address of the person's own. [taffyReadiness] is ready when this is
     * not empty, and a surface that names "the providers set up" names these,
     * so the two cannot disagree about which providers count.
     */
    val answeringProviderIds: Set<String>
        get() = usableCredentialProviderIds + ownEndpointProviderIds

    companion object {
        /** The facts before the core has said anything. */
        val UNKNOWN: ProviderReadinessFacts = ProviderReadinessFacts(known = false)
    }
}
