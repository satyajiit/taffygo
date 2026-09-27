// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.credentials

/**
 * What one bounded probe call proved about a key (decision
 * `docs/decisions/0083-a-pasted-key-is-proved-by-one-bounded-completion.md`).
 *
 * Ten members, three meanings. [USABLE] and the definitive failures
 * ([AUTH], [BILLING], [MODEL_NOT_FOUND]) are answers about the key; every
 * other member means the provider was not definitively heard — an
 * unreachable endpoint is not a wrong key, so an indefinite verdict leaves
 * the key unjudged and a surface offering to save anyway. One of them,
 * [NO_MODEL_LISTED], is filed without any call: nothing was sent, so the key
 * is unjudged for a reason a surface can name.
 */
enum class ProviderProbeVerdict {
    /** The provider answered the probe on this credential. */
    USABLE,

    /** Definitive: the provider refused the credential itself. */
    AUTH,

    /** Definitive: the account behind the credential cannot pay. */
    BILLING,

    /** Indefinite: the provider is rate limiting; the key was not judged. */
    RATE_LIMIT,

    /** Indefinite: the provider is overloaded; the key was not judged. */
    OVERLOADED,

    /** Indefinite: the call ran out of time before an answer. */
    TIMEOUT,

    /** Indefinite: the provider was not reached at all. */
    NETWORK,

    /**
     * Definitive: the probe's model is unknown at this endpoint — a catalog
     * defect rather than a key defect, and said as one.
     */
    MODEL_NOT_FOUND,

    /** Indefinite: the answer fit no closed category. */
    UNKNOWN,

    /**
     * Definitive about the catalog, not the key: the provider carries no model
     * this build could probe with, so nothing was sent and the key was not
     * judged. A provider that serves its own list is in this state until a
     * credential is saved and its list fetched (decision 0098 section 1), so a
     * surface offers to save — and can say why, which the transient "could not
     * run right now" this replaced never could.
     */
    NO_MODEL_LISTED,
    ;

    /** Whether this verdict is a definitive answer about the key. */
    val definitive: Boolean
        get() = when (this) {
            USABLE, AUTH, BILLING, MODEL_NOT_FOUND -> true
            RATE_LIMIT, OVERLOADED, TIMEOUT, NETWORK, UNKNOWN, NO_MODEL_LISTED -> false
        }
}
