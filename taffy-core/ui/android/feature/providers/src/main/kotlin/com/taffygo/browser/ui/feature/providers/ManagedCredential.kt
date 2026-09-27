// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.RosterRefusalState

/**
 * The card screen SCR-415 draws over a credential that is already stored.
 *
 * Facts about a record, never the record. Nothing here is material, a handle or
 * anything a person could paste somewhere else: the labels are the vendor's own
 * words about the account and the plan, shown so somebody with two accounts can
 * tell which one this is, and never parsed.
 *
 * It is one card whatever saved the credential. A key and a plan are two ways
 * of arriving and one thing to manage, so the card is written from the roster's
 * facts rather than from the method — which is also why a person who signed in
 * and a person who pasted see the same three actions in the same place.
 */
data class ManagedCredential(
    /** How the vendor names the signed-in account, null when it said nothing. */
    val accountLabel: String?,
    /** The plan the vendor says backs it, null when it said nothing. */
    val planLabel: String?,
    /** Whether a vendor plan rather than a metered key stands behind it. */
    val subscriptionBacked: Boolean,
    /**
     * Whether the core expects the credential to work. False is a transient
     * fact — a refresh that failed, a vendor that wants the person again — and
     * never the removal of anything, so the card stays and says so.
     */
    val confirmed: Boolean,
    /**
     * The model this person pinned, as the catalog names it. Null while the
     * provider's own order stands, which is a real answer and is said as one
     * rather than as a blank.
     */
    val modelName: String?,
    /**
     * Whether the vendor's sign-in can be run again from here. False for a
     * pasted key with no compiled flow behind it: re-authenticating is signing
     * in, and there is nothing to sign in to.
     */
    val canReauthenticate: Boolean,
    /**
     * The last refusal the vendor answered with, null when it has refused
     * nothing. Drawn beside [confirmed] rather than folded into it, because a
     * rate limit or an unpaid account is a working credential the vendor is
     * declining to spend, and the card must not send a person to replace a
     * key that was never the problem.
     */
    val lastRefusal: RosterRefusalState? = null,
)
