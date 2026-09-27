// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * One provider on screen SCR-404, as the screen will draw it.
 *
 * Every field is already decided when the row reaches a composable: the screen
 * chooses a word and a colour for what is here and asks the roster nothing.
 * That is what keeps the whole of SCR-404's behaviour provable by a host test
 * calling `ProviderHubProjection.project`.
 *
 * ## Nothing here describes a credential
 *
 * A row is on this screen precisely because the provider is *not* set up —
 * `ProviderRowDispatch.groupsFor` files a connected one under no group at all.
 * So there is no account, no plan, no availability and no standing-choice
 * badge: every one of them would be a field that could only ever hold one
 * value, and a reader would reasonably take their presence as evidence that
 * this screen still lists what is connected. `ConnectedProviderRow` carries
 * them, on the screen where they can vary.
 *
 * [signingIn] is the exception and is not a credential: a sign-in in flight has
 * not stored anything yet, which is exactly why the row is still here.
 */
data class ProviderHubRow(
    /** Stable identity, and the key any credential would be filed under. */
    val providerId: String,
    /** The name to show, as the catalog spells it. */
    val displayName: String,
    /** Which of the three groups this row is filed under. */
    val group: ProviderHubGroup,
    /** What pressing the row offers, or why nothing is offered. */
    val offer: ProviderRowOffer,
    /** How this provider could be reached, whether or not it has been. */
    val wayIn: ProviderWayIn,
    /** Whether a sign-in for this provider is running, as the roster reports it. */
    val signingIn: Boolean,
)
