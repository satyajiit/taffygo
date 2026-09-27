// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/** Everything screen SCR-419 can be asked to do. */
sealed interface ConnectedProvidersIntent {

    /**
     * Go where this row leads.
     *
     * The row carries its own offer, exactly as the hub's does, so this intent
     * names no destination and the screen cannot send a person somewhere their
     * provider does not support.
     */
    data class OpenRow(val row: ConnectedProviderRow) : ConnectedProvidersIntent

    /** Ask before removing this row's credential. */
    data class AskSignOut(val row: ConnectedProviderRow) : ConnectedProvidersIntent

    /** Remove it. Reachable only from the confirmation. */
    data object ConfirmSignOut : ConnectedProvidersIntent

    /** Leave the credential alone. */
    data object CancelSignOut : ConnectedProvidersIntent

    /**
     * Go and connect another one.
     *
     * This screen lists what is set up and never adds, so the only thing it
     * can do about adding is open the screen that does — SCR-404, whose whole
     * subject is what is not connected yet.
     */
    data object AddProvider : ConnectedProvidersIntent
}
