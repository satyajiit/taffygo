// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderPresentation

/**
 * Screen SCR-415 — one provider's own page.
 *
 * Everything a person can do about one provider is here and only here: the
 * credential, the model it is pinned to, and whether Taffy sends its requests
 * this way. The hub selects; this page changes things.
 *
 * The order of the fields is the order of the page, and it is the argument the
 * page makes. A stored credential comes first because somebody already
 * connected is here to manage rather than to set up. Then the ways in, with the
 * plan above the key: signing in to something already paid for is one tap and
 * pasting a key is an errand, so the shorter road is offered first and the key
 * sits under a divider rather than beside it. Never two primary actions.
 */
data class ProviderConfigUiState(
    /** Whether there is a provider to draw, and what to say when there is not. */
    val status: Status = Status.LOADING,
    /** The catalog identity this page is about. */
    val providerId: String = "",
    /** The name to show, as the catalog spells it. */
    val displayName: String = "",
    /**
     * Why nothing on this page can be acted on, or null. The three roster
     * refusals plus a catalog entry naming no way in at all: each one is a
     * reason the product has already given, and a person must not be walked
     * into a form that is about to refuse them.
     */
    val blocked: ProviderRowOffer.Reason? = null,
    /** Whether the vendor's sign-in is on offer, absent, or explained. */
    val signIn: ProviderSignInOffer = ProviderSignInOffer.NONE,
    /** The key form, or null when this provider takes no key. */
    val keyForm: ProviderKeyForm? = null,
    /** The stored credential, or null when nothing is stored. */
    val managed: ManagedCredential? = null,
    /**
     * The catalog's few behavioural facts for this provider, or null when it
     * said none. Absent means show nothing — never a placeholder link.
     */
    val presentation: ProviderPresentation? = null,
    /** Where "Taffy uses this provider" stands. */
    val defaultChoice: ProviderDefaultChoice = ProviderDefaultChoice.UNAVAILABLE,
    /** Whether the second, deliberate step before removing a credential shows. */
    val confirmingSignOut: Boolean = false,
    /** Whether the removal is running. */
    val signingOut: Boolean = false,
) {
    /**
     * Whether the page shows both ways in and therefore needs the "or paste a
     * key" rule between them. A page with one way in has nothing to separate.
     */
    val separatesWaysIn: Boolean
        get() = signIn != ProviderSignInOffer.NONE && keyForm != null

    /** Whether anything on the page can be pressed at all. */
    val actionable: Boolean get() = status == Status.READY && blocked == null

    /** Whether this page has a way in to offer beside what is already stored. */
    val offersAWayIn: Boolean get() = keyForm != null || signIn == ProviderSignInOffer.OFFERED

    /**
     * What the page has to say when it has no provider to draw.
     *
     * [UNKNOWN] is not an error. The catalog is served, so a provider can leave
     * it between one snapshot and the next, and a page opened from a stale back
     * stack is then about something the browser no longer carries. Saying so is
     * the whole of the answer; a spinner would wait for something that is not
     * coming.
     */
    enum class Status {
        /** The core has not published a roster yet. */
        LOADING,

        /** The roster arrived and does not carry this provider. */
        UNKNOWN,

        /** There is a provider to draw. */
        READY,
    }
}
