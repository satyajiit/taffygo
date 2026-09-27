// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * Where "Taffy uses this provider" stands for one provider, in four answers.
 *
 * The product holds exactly one fact about where a model request goes: the
 * route (`ProviderRoute`), which says whether the person's own credential is
 * the way in and never names a provider. So a default is derived here the same
 * conservative way screen SCR-404 derives its badge — one usable credential is
 * unambiguous, several are not — and the two cases where nothing can honestly
 * be claimed are members rather than a silently disabled button.
 *
 * [SHARED] is the one that has to exist. Two stored credentials with the direct
 * route standing is a real and ordinary state, and the product has no fact that
 * settles which of them is in force; a control that pretended to pin one would
 * be writing nothing and reporting success. So the screen says what is true and
 * offers no control at all.
 */
enum class ProviderDefaultChoice {
    /**
     * No credential stands behind this provider, so there is nothing to send a
     * request with and nothing to make default.
     */
    UNAVAILABLE,

    /**
     * Choosing it would change where requests go, because nothing is settled
     * yet: this provider is configured and is not the one a request would
     * reach. The ordinary way in is a credential the browser holds and the
     * core's roster has not echoed back, which is configured without being
     * usable, so it stands behind no request until it is confirmed.
     */
    OFFERED,

    /** A request would go here right now, and the screen states it. */
    IN_FORCE,

    /**
     * Requests go to a provider of your own and more than one is connected.
     * The product cannot name which, so neither does the screen.
     */
    SHARED,
}
