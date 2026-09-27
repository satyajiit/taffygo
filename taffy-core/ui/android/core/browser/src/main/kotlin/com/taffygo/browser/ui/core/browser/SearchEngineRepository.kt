// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import kotlinx.coroutines.flow.StateFlow

/**
 * The chosen address-bar search engine, and the list the picker shows.
 *
 * DefaultBrowserRepository asks this for an address rather than holding a
 * host of its own. Model grounding is not this port: that question is OD-019.
 */
interface SearchEngineRepository {

    /** The engine the address bar will use. */
    val selectedId: StateFlow<SearchEngineId>

    /** Engines offered for [regionCode], always including the current choice. */
    fun listed(regionCode: String): List<SearchEngine>

    /**
     * The address [query] should open, or null when there is no engine or
     * nothing to send.
     */
    fun searchUrl(query: String): String?

    /**
     * The address Taffy opens for a task's [query] — the same engine, asked
     * for English. See [SearchEngineCatalog.taskSearchUrl] for why a task's
     * search and a person's differ.
     */
    fun taskSearchUrl(query: String): String?

    /** Make [id] the address-bar engine. */
    suspend fun select(id: SearchEngineId)
}
