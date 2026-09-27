// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import kotlinx.coroutines.flow.StateFlow

/**
 * The sites the person actually returns to, counted on this device only.
 *
 * The store is profile-scoped where [BrowserRepository] is window-scoped,
 * because frequency is a fact about the person's browsing rather than about
 * one window's open tabs: two windows feed one count, and the count survives
 * both of them.
 *
 * What is counted is decided by the one recorder
 * (`FrequentSitesTracker`): a committed navigation in a tab that is not
 * private and not Taffy's. A private tab must leave no trace here for the
 * reason it exists, and Taffy's own tabs are not the person's habits.
 */
interface FrequentSitesRepository {

    /** Every counted site, most visited first, bounded and persisted. */
    val sites: StateFlow<List<FrequentSite>>

    /** Count one committed navigation to [host], carrying the page's [title]. */
    suspend fun recordVisit(host: String, title: String)
}
