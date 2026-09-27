// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One provider as the merged catalog serves it to a surface (decision 0080).
 *
 * A projection of the Core API roster entry into the app's own vocabulary, so
 * a feature never names a generated wire type. Nothing here is decided by
 * Android: every field is the core's answer, and the one intersection a
 * surface performs — "connected" is a roster state *and* a browser-held
 * credential — happens where both halves are in hand.
 */
data class ProviderRosterRow(
    /** Stable identity, and the key any credential is filed under. */
    val providerId: String,
    /** The name to show, as the catalog spells it. */
    val displayName: String,
    /** Catalog row or the person's own provider. */
    val origin: RosterProviderOrigin,
    /** Every method this provider offers, in catalog order. */
    val authMethods: List<RosterAuthMethod>,
    /** The stored credential's registry facts, null when nothing is stored. */
    val stored: RosterStoredCredential?,
    /** Whether a sign-in flow is currently admitted and pending. */
    val signingIn: Boolean,
    /**
     * The catalog kill switch. A disabled provider is still listed so the
     * screen can say it was switched off rather than hiding it.
     */
    val enabled: Boolean,
    /**
     * Whether this build can act on the row at all. A served row must not
     * claim what the binary cannot do, so an unconfigurable provider is
     * listed and explained, never offered.
     */
    val configurable: Boolean,
    /**
     * Whether OAUTH on this row means a plan the person already pays for, as
     * the catalog states it.
     *
     * Carried rather than derived from [authMethods], because the two are not
     * the same question: a vendor whose sign-in mints a metered key is spent
     * exactly as a pasted one is, and grouping it with plans would put a
     * subscription sentence in front of somebody billed by the token.
     */
    val subscription: Boolean,
    /** Which catalog layer supplied this row. */
    val catalogLayer: RosterCatalogLayer,
    /**
     * The model this person pinned for this provider, null while the provider's
     * own order stands. Always a model the catalog still carries: a pin whose
     * model leaves the catalog is dropped rather than shown against nothing.
     */
    val selectedModelId: String?,
    /** How much thinking this person asked this provider for, null while Taffy decides. */
    val thinking: ThinkingLevel?,
    /** The catalog's few behavioural facts for a setup surface, null when it said none. */
    val presentation: ProviderPresentation?,
    /**
     * The whole address a person typed for a provider of their own, port and
     * base path included, null for every catalog row and for a custom row the
     * core has no address for.
     *
     * A separate fact from the host the roster carries for disclosure text,
     * which promises no port and no path. This one is the address itself, so an
     * edit screen can fill the field back in with what was registered instead
     * of starting empty and explaining why.
     */
    val endpointBase: String?,
    /**
     * The last refusal this provider answered with, null when it has refused
     * nothing.
     *
     * Carried beside [stored] rather than folded into it, because a refusal is
     * a fact about a request and [RosterStoredCredential.state] is a fact about
     * the credential: a key that is refused for a rate limit or an unpaid
     * account is a key that works, and saying otherwise would send a person to
     * re-enter a key that was never the problem.
     */
    val lastRefusal: RosterRefusalState?,
    /**
     * How many models this provider carries in the merged catalog, counted by
     * the core before the flat model list was fitted to its shared budget.
     *
     * Beside that list rather than counted out of it. The list is what survived
     * the budget every provider shares, so counting a provider's rows in it
     * answers "how many are on screen", not "how many are there" — and the
     * difference between the two is exactly the rows this provider lost.
     * Zero means the catalog names no model for this provider.
     */
    val modelCount: Int,
)
