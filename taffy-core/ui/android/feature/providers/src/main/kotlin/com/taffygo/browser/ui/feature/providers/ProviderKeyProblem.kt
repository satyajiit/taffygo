// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * Safe, content-free failures the key form on screen SCR-415 can render.
 *
 * A probe verdict is never shown raw. The provider's own body, the keystore's
 * diagnostic and the core's refusal are all things a person cannot act on and
 * two of them can carry material, so each one arrives here as a category and
 * leaves as a sentence in the person's own language.
 *
 * [savableAnyway] separates the two kinds of probe answer (decision
 * `docs/decisions/0083-a-pasted-key-is-proved-by-one-bounded-completion.md`): a
 * definitive refusal is about the key and keeps the draft for correction, while
 * an indefinite one means the provider was not definitively heard — an
 * unreachable endpoint is not a wrong key — so the form offers to save anyway.
 * [NO_MODEL_LISTED] is the same offer reached without a call at all: what was
 * judged there is the catalog, and a key nothing was spent on is unjudged.
 */
enum class ProviderKeyProblem(val savableAnyway: Boolean = false) {
    /** The field contained no non-whitespace character. */
    EMPTY,

    /**
     * The pasted value does not begin the way this vendor's keys begin. Caught
     * before any call: the catalog says the prefix, so spending a probe to
     * learn what the form already knows costs a person a round trip for
     * nothing.
     */
    PREFIX_MISMATCH,

    /** The encrypted device store refused the operation. */
    STORE_FAILED,

    /** Definitive: the provider refused the key itself. */
    KEY_REFUSED,

    /** Definitive: the account behind the key cannot pay. */
    BILLING_REFUSED,

    /**
     * Definitive: the probe's model is unknown at this endpoint — a catalog
     * defect rather than a key defect, and said as one.
     */
    MODEL_NOT_FOUND,

    /** Indefinite: the provider is rate limiting; the key was not judged. */
    RATE_LIMITED(savableAnyway = true),

    /** Indefinite: the provider is overloaded; the key was not judged. */
    OVERLOADED(savableAnyway = true),

    /** Indefinite: the test ran out of time before an answer. */
    TIMED_OUT(savableAnyway = true),

    /** Indefinite: the provider was not reached at all. */
    UNREACHABLE(savableAnyway = true),

    /** Indefinite: the test's answer fit no closed category. */
    UNSETTLED(savableAnyway = true),

    /** Indefinite: the test could not run at all right now. */
    TEST_UNAVAILABLE(savableAnyway = true),

    /**
     * The catalog was judged, not the key: this provider carries no model this
     * build could probe with, so nothing was sent (decision 0098 section 1).
     *
     * Savable for a reason the other indefinite members do not have — a
     * provider that serves its own list stays in this state until a credential
     * is saved and its list fetched, so refusing the save would be refusing the
     * one thing that ends it.
     */
    NO_MODEL_LISTED(savableAnyway = true),
}
