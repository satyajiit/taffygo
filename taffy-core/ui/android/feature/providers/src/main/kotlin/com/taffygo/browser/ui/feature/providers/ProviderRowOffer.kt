// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * What one provider row offers a person, and therefore where pressing it goes.
 *
 * The whole of screen SCR-404's behaviour is this one value. It is decided by
 * `ProviderRowDispatch` from the roster's facts about a provider and from
 * nothing else — never from the provider's identity — so a provider a catalog
 * update introduces behaves correctly on a binary that has never heard of it.
 *
 * [Blocked] is a first-class answer rather than the absence of one. A row this
 * build cannot act on is listed, says why, and cannot be pressed; degrading it
 * into some other offer would hand a person a control that cannot work, which
 * is worse than an absent one.
 */
sealed interface ProviderRowOffer {

    /** The provider's own page: its credential, its model, its thinking. */
    data object Configure : ProviderRowOffer

    /** The vendor's sign-in, which this binary compiles a flow for. */
    data object SignIn : ProviderRowOffer

    /** The endpoint the person supplied, and the answers about it. */
    data object EditEndpoint : ProviderRowOffer

    /** Listed and explained, never offered. */
    data class Blocked(val reason: Reason) : ProviderRowOffer

    /** Why a listed row cannot be acted on, in the roster's own terms. */
    enum class Reason {
        /**
         * The roster says this build cannot act on the row at all. A served row
         * must not claim what the binary cannot do.
         */
        NOT_ACTIONABLE,

        /** The catalog's kill switch is on for this provider. */
        HELD_SHUT,

        /**
         * The row offers only a vendor sign-in, and this binary compiles no
         * flow for that vendor. A sign-in names pinned origins and a shape,
         * which is binary behaviour a served catalog cannot invent — so the
         * honest rendering is available-and-not-startable, never a key form.
         *
         * Reachable because the catalog is served (decision 0080): a vendor
         * added to the roster after this binary shipped is exactly this row.
         */
        SIGN_IN_NOT_BUILT,

        /**
         * The flow is built and the browser will not start it, because nobody
         * has dated a review of what that vendor's terms permit.
         *
         * Separate from [SIGN_IN_NOT_BUILT] because they are different facts
         * with different remedies and only one of them is about the binary. A
         * missing flow is answered by a release; an undated review is answered
         * by somebody reading the vendor's terms, which decision 0081 makes an
         * owner's act. Telling a person the sign-in is "not in this version"
         * when it is compiled, tested and one date away would be a false
         * sentence about a product they cannot do anything about either way.
         */
        SIGN_IN_NOT_CLEARED,

        /** The row names no way in at all, which is a defect in its catalog entry. */
        NO_METHOD,
    }
}
