// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * An in-memory engine choice. Host tests and previews use this; the window
 * graph uses a store-backed implementation.
 */
class MemorySearchEngineRepository(
    initial: SearchEngineId = SearchEngineId.DEFAULT,
    private val canSearch: Boolean = true,
) : SearchEngineRepository {

    private val current = MutableStateFlow(initial)

    override val selectedId: StateFlow<SearchEngineId> = current.asStateFlow()

    override fun listed(regionCode: String): List<SearchEngine> =
        SearchEngineCatalog.listed(regionCode, current.value)

    override fun searchUrl(query: String): String? {
        if (!canSearch) return null
        return SearchEngineCatalog.searchUrl(current.value, query)
    }

    override fun taskSearchUrl(query: String): String? {
        if (!canSearch) return null
        return SearchEngineCatalog.taskSearchUrl(current.value, query)
    }

    override suspend fun select(id: SearchEngineId) {
        current.value = id
    }
}
