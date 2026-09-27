// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The registry facts of one stored provider credential.
 *
 * Facts about a record, never the record: no material, no handle. The browser
 * secure store is the other half of "connected", which a surface intersects
 * with this rather than trusting either alone.
 */
data class RosterStoredCredential(
    /** The method the stored credential was saved under. */
    val authMethod: RosterAuthMethod,
    /** What the registry says the credential can do right now. */
    val state: RosterCredentialState,
    /** Whether a vendor plan, rather than a metered key, stands behind it. */
    val subscriptionBacked: Boolean,
    /**
     * How the vendor names the signed-in account, exactly as the vendor said it
     * and null whenever the sign-in returned none. Shown so a person can tell
     * which of their accounts is connected; never parsed, and never matched
     * against the product's own account.
     */
    val accountLabel: String?,
    /**
     * The plan the vendor says backs this credential, in the vendor's own
     * words. Null whenever the vendor said nothing, which is not the same as no
     * plan and is never rendered as one.
     */
    val planLabel: String?,
)
