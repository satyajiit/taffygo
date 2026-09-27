// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.RosterRefusalState
import com.taffygo.browser.ui.core.model.ThinkingLevel

/**
 * One provider on screen SCR-419, as the screen will draw it.
 *
 * The hub (SCR-404) answers "what could I use"; this list answers "what am I
 * using, and what is it set to". So the fields are the ones the hub leaves
 * out: the model a request would actually be sent to and how much thinking it
 * is asked for. Everything else is decided before the row reaches a
 * composable, exactly as `ProviderHubRow` is, which is what keeps the whole of
 * this screen provable by a host test.
 *
 * [canSignOut] and [ownEndpoint] are separate facts and neither implies the
 * other. A provider a person defined can hold a credential, and a catalog
 * provider never becomes one of their own — so a row can offer signing out,
 * or editing an address, or both.
 */
data class ConnectedProviderRow(
    /** Stable identity, and the key any credential is filed under. */
    val providerId: String,
    /** The name to show, as the roster spells it. */
    val displayName: String,
    /** Whether a credential stands behind this provider, three-valued. */
    val availability: CredentialAvailability,
    /** How the vendor names the signed-in account, null when it said nothing. */
    val accountLabel: String?,
    /** The plan the vendor says backs the credential, null when it said nothing. */
    val planLabel: String?,
    /**
     * The name of the pinned model, or null while the provider's own order
     * stands. Never the identifier: a model id is a wire value and reads like
     * one.
     */
    val modelName: String?,
    /** How much thinking this provider is asked for, null while Taffy decides. */
    val thinking: ThinkingLevel?,
    /** Whether the person supplied this provider's address themselves. */
    val ownEndpoint: Boolean,
    /** Whether a model request would go here right now. */
    val carriesStandingChoice: Boolean,
    /** Whether there is a credential on this phone to remove. */
    val canSignOut: Boolean,
    /** Whether this row's removal is running. */
    val signingOut: Boolean,
    /** Where pressing the row leads, in the hub's own vocabulary. */
    val offer: ProviderRowOffer,
    /**
     * The last refusal this provider answered with, null when it has refused
     * nothing. A fact about a request, never about the credential: the row
     * stays connected and says the vendor is not spending it right now.
     */
    val lastRefusal: RosterRefusalState? = null,
)
