// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/** Everything screen SCR-404 can be asked to do. */
sealed interface ProviderHubIntent {

    /**
     * Show one category of providers.
     *
     * The only intent on this screen that changes what is drawn rather than
     * where the person goes, which is why it is the only one the reducer has
     * anything to do with.
     */
    data class ShowCategory(val group: ProviderHubGroup) : ProviderHubIntent

    /**
     * Go where this row leads.
     *
     * The row carries its own offer, so this intent names no destination: the
     * hub cannot send a person to a key form for a provider that offers no
     * key, because it never chooses a destination at all.
     */
    data class OpenRow(val row: ProviderHubRow) : ProviderHubIntent

    /** Set up a provider whose address the person supplies. */
    data object AddYourOwnProvider : ProviderHubIntent
}
