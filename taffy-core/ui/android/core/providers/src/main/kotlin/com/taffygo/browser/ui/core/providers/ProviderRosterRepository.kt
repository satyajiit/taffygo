// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providers

import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderRosterState
import kotlinx.coroutines.flow.StateFlow

/**
 * The provider roster the isolated core last published.
 *
 * Read-only on purpose. Everything a person does about a provider — saving a
 * key, forgetting one, signing in — goes through its own seam and comes back
 * as the next published roster; nothing here accepts an intent, so nothing
 * here can disagree with the core about what happened.
 */
interface ProviderRosterRepository {

    /** Every provider the merged catalog carries, and whether the list is real. */
    val roster: StateFlow<ProviderRosterState>

    /**
     * Every model a surface may currently offer, under the provider that
     * carries it.
     *
     * The core publishes one flat, bounded list across every provider, so the
     * bound holds for the snapshot rather than for each provider separately;
     * this is that list read the way a screen needs it. A provider with no
     * entry has had no model named for it, and [roster] rather than this is
     * what says the core has spoken at all.
     */
    val models: StateFlow<Map<String, List<ProviderModel>>>
}
